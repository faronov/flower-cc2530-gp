"""ZHA Green Power quirk for the DIYRuZ FlowerX soil sensor GPD prototype (flower-gp).

The GPD commissions with DeviceID 0x33 (IndoorEnvironmentSensor), manufacturer 0x6D6B
and model 0xF100 | sensors (bit 0: BME280, bit 1: DS18B20), then sends unsecured
Multi-Cluster Reporting (0xA2) frames carrying the ZCL MeasuredValue encodings of the
regular FlowerX firmware. A BME280 adds humidity (0x0405) and pressure (0x0403, hPa);
a DS18B20 adds the soil temperature as 0x0402 attribute 0x5000.
Copy to <HA config>/custom_zha_quirks/ (requires ZHA with Green Power support).
"""

from __future__ import annotations

from typing import Any

from zha.application.platforms.sensor import BaseSensor
from zha.application.platforms.sensor.const import SensorDeviceClass, SensorStateClass
from zha.units import (
    LIGHT_LUX,
    PERCENTAGE,
    UnitOfElectricPotential,
    UnitOfPressure,
    UnitOfTemperature,
)
from zigpy.device import GreenPowerCommandReceived
from zigpy.zgp.types import DeviceID, GPDCommandID

from zhaquirks.builder import GreenPowerQuirkBuilder

FLOWER_GP_MANUFACTURER_ID = 0x6D6B
FLOWER_GP_MODEL_ID = 0xF100
FLOWER_GP_HAS_BME280 = 0x01
FLOWER_GP_HAS_DS18B20 = 0x02


class FlowerGreenPowerSensor(BaseSensor):
    """Sensor updated from one ZCL attribute in the GPD attribute reports."""

    _cluster_id: int
    _attribute_id: int
    _invalid: int
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, device: Any, **kwargs: Any) -> None:
        """Initialize the sensor without a value."""
        self._value: float | None = None
        super().__init__(device, **kwargs)

    @staticmethod
    def convert(raw: int) -> float:
        """Convert the raw ZCL value."""
        raise NotImplementedError

    @property
    def native_value(self) -> float | None:
        """Return the last reported value."""
        return self._value

    def on_add(self) -> None:
        """Subscribe to the commands sent by the GPD."""
        super().on_add()
        self._on_remove_callbacks.append(
            self.device.device.on_event(
                GreenPowerCommandReceived.event_type, self._handle_gp_command
            )
        )

    def _handle_gp_command(self, event: GreenPowerCommandReceived) -> None:
        if event.command is None:
            return
        if event.command_id == GPDCommandID.MultiClusterReporting:
            records = [(r.cluster_id, r.attribute) for r in event.command.reports]
        elif event.command_id == GPDCommandID.AttributeReporting:
            records = [(event.command.cluster_id, a) for a in event.command.attributes]
        else:
            return
        for cluster_id, attribute in records:
            if cluster_id == self._cluster_id and attribute.attrid == self._attribute_id:
                raw = attribute.value.value
                self._value = None if raw == self._invalid else self.convert(raw)
                self.maybe_emit_state_changed_event()


class FlowerTemperature(FlowerGreenPowerSensor):
    """Temperature (0x0402 MeasuredValue, 0.01 °C)."""

    _cluster_id, _attribute_id, _invalid = 0x0402, 0x0000, -0x8000
    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_suggested_display_precision = 1

    @staticmethod
    def convert(raw: int) -> float:
        """Convert hundredths of a degree."""
        return raw / 100


class FlowerSoilMoisture(FlowerGreenPowerSensor):
    """Soil moisture (0x0408 MeasuredValue, 0.01 %)."""

    _cluster_id, _attribute_id, _invalid = 0x0408, 0x0000, 0xFFFF
    _attr_device_class = SensorDeviceClass.MOISTURE
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_suggested_display_precision = 0

    @staticmethod
    def convert(raw: int) -> float:
        """Convert hundredths of a percent."""
        return raw / 100


class FlowerIlluminance(FlowerGreenPowerSensor):
    """Illuminance (0x0400 MeasuredValue, 10000*log10(lx)+1)."""

    _cluster_id, _attribute_id, _invalid = 0x0400, 0x0000, 0xFFFF
    _attr_device_class = SensorDeviceClass.ILLUMINANCE
    _attr_native_unit_of_measurement = LIGHT_LUX
    _attr_suggested_display_precision = 0

    @staticmethod
    def convert(raw: int) -> float:
        """Convert the logarithmic ZCL encoding to lux."""
        return 0.0 if raw == 0 else round(10 ** ((raw - 1) / 10000), 1)


class FlowerSoilTemperature(FlowerTemperature):
    """DS18B20 soil temperature (0x0402 manufacturer attribute 0x5000, 0.01 °C)."""

    _attribute_id = 0x5000


class FlowerHumidity(FlowerGreenPowerSensor):
    """BME280 humidity (0x0405 MeasuredValue, 0.01 %)."""

    _cluster_id, _attribute_id, _invalid = 0x0405, 0x0000, 0xFFFF
    _attr_device_class = SensorDeviceClass.HUMIDITY
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_suggested_display_precision = 0

    @staticmethod
    def convert(raw: int) -> float:
        """Convert hundredths of a percent."""
        return raw / 100


class FlowerPressure(FlowerGreenPowerSensor):
    """BME280 pressure (0x0403 MeasuredValue, 0.1 kPa)."""

    _cluster_id, _attribute_id, _invalid = 0x0403, 0x0000, -0x8000
    _attr_device_class = SensorDeviceClass.PRESSURE
    _attr_native_unit_of_measurement = UnitOfPressure.HPA
    _attr_suggested_display_precision = 0

    @staticmethod
    def convert(raw: int) -> float:
        """0.1 kPa equals 1 hPa."""
        return float(raw)


class FlowerBattery(FlowerGreenPowerSensor):
    """Battery remaining (0x0001 BatteryPercentageRemaining, 0.5 %)."""

    _cluster_id, _attribute_id, _invalid = 0x0001, 0x0021, 0xFF
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_suggested_display_precision = 0

    @staticmethod
    def convert(raw: int) -> float:
        """Convert half-percent steps."""
        return raw / 2


class FlowerBatteryVoltage(FlowerGreenPowerSensor):
    """Battery voltage (0x0001 BatteryVoltage, 100 mV)."""

    _cluster_id, _attribute_id, _invalid = 0x0001, 0x0020, 0xFF
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_native_unit_of_measurement = UnitOfElectricPotential.VOLT
    _attr_suggested_display_precision = 1

    @staticmethod
    def convert(raw: int) -> float:
        """Convert 100 mV units."""
        return raw / 10


for _sensors in range(4):
    _quirk = (
        GreenPowerQuirkBuilder()
        .applies_to(
            device_id=DeviceID.IndoorEnvironmentSensor,
            manufacturer_id=FLOWER_GP_MANUFACTURER_ID,
            model_id=FLOWER_GP_MODEL_ID | _sensors,
        )
        .adds_entity(
            FlowerSoilMoisture,
            unique_id_suffix="soil_moisture",
            fallback_name="Soil moisture",
        )
        .adds_entity(
            FlowerTemperature, unique_id_suffix="temperature", fallback_name="Temperature"
        )
        .adds_entity(
            FlowerIlluminance, unique_id_suffix="illuminance", fallback_name="Illuminance"
        )
        .adds_entity(FlowerBattery, unique_id_suffix="battery", fallback_name="Battery")
        .adds_entity(
            FlowerBatteryVoltage,
            unique_id_suffix="battery_voltage",
            fallback_name="Battery voltage",
        )
    )
    if _sensors & FLOWER_GP_HAS_BME280:
        _quirk.adds_entity(
            FlowerHumidity, unique_id_suffix="humidity", fallback_name="Humidity"
        ).adds_entity(
            FlowerPressure, unique_id_suffix="pressure", fallback_name="Pressure"
        )
    if _sensors & FLOWER_GP_HAS_DS18B20:
        _quirk.adds_entity(
            FlowerSoilTemperature,
            unique_id_suffix="soil_temperature",
            fallback_name="Soil temperature",
        )
    _quirk.friendly_name(manufacturer="DIYRuZ", model="FlowerX GP").add_to_registry()
