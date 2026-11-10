"""Test the flower-gp ZHA quirk against frames produced by include/gpd_frame.h."""

from unittest.mock import MagicMock

import pytest

from zha.quirks import DEVICE_REGISTRY
from zigpy.device import GreenPowerCommandReceived, GreenPowerDevice
from zigpy.zgp.commands import GPCommissioningPayload, GPMultiClusterReportingPayload
from zigpy.zgp.types import ApplicationID, GPDCommandID, SrcID

import zha_quirk_diyruz_flowerx_gp  # noqa: F401  registers the quirk

# tests/test_gpd_frame.c frames for IEEE 00:12:4b:00:0e:f6:28:15, after byte 13.
COMMISSIONING = bytes.fromhex("33450b6b6d00f1040100000402040804")
REPORT = bytes.fromhex(
    "02040000290cfe080400002110270004000021ffff01002000201e0100210020c8"
)
# The same frames with a BME280 and a DS18B20 (model 0xF103).
COMMISSIONING_ALL = bytes.fromhex("33450b6b6d03f106010000040204080405040304")
REPORT_ALL = REPORT + bytes.fromhex("05040000218f170304000029f5030204005029e204")
# tests/test_gpd_frame.c with BOARD_BGT1 (model 0xF202).
BGT1_COMMISSIONING = bytes.fromhex("30450b6b6d02f20201000204")
BGT1_REPORT = bytes.fromhex("02040000290cfe01002000201e0100210020c8")
BASE = {"soil_moisture", "temperature", "illuminance", "battery", "battery_voltage"}


def _report(command):
    return GreenPowerCommandReceived(
        device_ieee="", endpoint_id=0,
        command_id=GPDCommandID.MultiClusterReporting, command=command,
    )


def _device(frame, name="DIYRuZ FlowerX GP"):
    commissioning, rest = GPCommissioningPayload.deserialize(frame)
    assert rest == b""
    gpd = GreenPowerDevice(
        MagicMock(), application_id=ApplicationID.SrcID, src_id=SrcID(0x0EF62815)
    )
    gpd.device_id = commissioning.device_id
    gpd.gpd_manufacturer_id = commissioning.manufacturer_id
    gpd.gpd_model_id = commissioning.model_id
    entry = DEVICE_REGISTRY.match_green_power_entry(gpd)
    assert entry is not None
    zha_device = entry.zha_device_factory(DEVICE_REGISTRY.resolve(gpd), MagicMock())
    assert zha_device.name == name
    entities = {e.unique_id.rsplit("-", 1)[1]: e for e in zha_device.discover_entities()}
    for entity in entities.values():
        entity.on_add()
        assert entity.native_value is None
    return gpd, entities


@pytest.mark.parametrize(
    ("model", "extra"),
    [
        (0xF101, {"humidity", "pressure"}),
        (0xF102, set()),
    ],
)
def test_flower_gp_variants(model, extra):
    frame = bytearray(COMMISSIONING)
    frame[5:7] = model.to_bytes(2, "little")
    _, entities = _device(bytes(frame))
    assert set(entities) == BASE | extra


def test_flower_gp_quirk_all_sensors():
    gpd, entities = _device(COMMISSIONING_ALL)
    assert set(entities) == BASE | {"humidity", "pressure", "soil_temperature"}
    report, rest = GPMultiClusterReportingPayload.deserialize(REPORT_ALL)
    assert rest == b""
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    values = {name: e.native_value for name, e in entities.items()}
    assert values == {
        "temperature": -5.0, "soil_moisture": 100.0, "illuminance": None,
        "battery_voltage": 3.0, "battery": 100.0,
        "humidity": 60.31, "pressure": 1013.0, "soil_temperature": 12.5,
    }


def test_flower_gp_quirk():
    gpd, entities = _device(COMMISSIONING)
    assert set(entities) == BASE
    soil = entities["soil_moisture"]
    assert soil.translation_key == "soil_moisture"
    assert not soil.primary

    report, rest = GPMultiClusterReportingPayload.deserialize(REPORT)
    assert rest == b""
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    values = {name: e.native_value for name, e in entities.items()}
    assert values == {
        "temperature": -5.0, "soil_moisture": 100.0, "illuminance": None,
        "battery_voltage": 3.0, "battery": 100.0,
    }

    report.reports[2].attribute.value.value = 1  # 10000*log10(1 lx)+1
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    assert entities["illuminance"].native_value == 1.0

    gpd.emit(GreenPowerCommandReceived.event_type, _report(None))
    assert entities["illuminance"].native_value == 1.0


@pytest.mark.parametrize("model", [0xF200, 0xF202])
def test_bgt1_gp_quirk(model):
    frame = bytearray(BGT1_COMMISSIONING)
    frame[5:7] = model.to_bytes(2, "little")
    gpd, entities = _device(bytes(frame), "EfektaLab BGT1 GP")
    assert set(entities) == {"temperature", "battery", "battery_voltage"}
    report, rest = GPMultiClusterReportingPayload.deserialize(BGT1_REPORT)
    assert rest == b""
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    values = {name: e.native_value for name, e in entities.items()}
    assert values == {"temperature": -5.0, "battery_voltage": 3.0, "battery": 100.0}
