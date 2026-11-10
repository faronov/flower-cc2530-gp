"""ZHA Green Power quirk for the EfektaLab BGT1 temperature sensor GPD build (bgt1-gp).

The GPD commissions with DeviceID 0x30 (TemperatureSensor), manufacturer 0x6D6B and
model 0xF200 | sensors (bit 1: DS18B20), then sends unsecured Multi-Cluster Reporting
(0xA2) frames with the DS18B20 temperature (0x0402), battery voltage (0x0001/0x0020)
and battery remaining (0x0001/0x0021) in their ZCL encodings.
Copy to <HA config>/custom_zha_quirks/ (requires ZHA with Green Power support).
"""

from __future__ import annotations

from typing import Any

from zha.application.platforms.sensor import BaseSensor
from zha.application.platforms.sensor.const import SensorDeviceClass, SensorStateClass
from zha.units import PERCENTAGE, UnitOfElectricPotential, UnitOfTemperature
from zigpy.device import GreenPowerCommandReceived
from zigpy.zgp.types import DeviceID, GPDCommandID

from zhaquirks.builder import GreenPowerQuirkBuilder

BGT1_GP_MANUFACTURER_ID = 0x6D6B
BGT1_GP_MODEL_ID = 0xF200
BGT1_GP_HAS_DS18B20 = 0x02


class Bgt1GreenPowerSensor(BaseSensor):
    """Sensor updated from one ZCL attribute in the GPD attribute reports."""

    _cluster_id: int
    _attribute_id: int
    _invalid: int
    _scale: int
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, device: Any, **kwargs: Any) -> None:
        """Initialize the sensor without a value."""
        self._value: float | None = None
        super().__init__(device, **kwargs)

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
                self._value = None if raw == self._invalid else raw / self._scale
                self.maybe_emit_state_changed_event()


class Bgt1Temperature(Bgt1GreenPowerSensor):
    """DS18B20 temperature (0x0402 MeasuredValue, 0.01 °C)."""

    _cluster_id, _attribute_id, _invalid, _scale = 0x0402, 0x0000, -0x8000, 100
    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_suggested_display_precision = 1


class Bgt1Battery(Bgt1GreenPowerSensor):
    """Battery remaining (0x0001 BatteryPercentageRemaining, 0.5 %)."""

    _cluster_id, _attribute_id, _invalid, _scale = 0x0001, 0x0021, 0xFF, 2
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_suggested_display_precision = 0


class Bgt1BatteryVoltage(Bgt1GreenPowerSensor):
    """Battery voltage (0x0001 BatteryVoltage, 100 mV)."""

    _cluster_id, _attribute_id, _invalid, _scale = 0x0001, 0x0020, 0xFF, 10
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_native_unit_of_measurement = UnitOfElectricPotential.VOLT
    _attr_suggested_display_precision = 1


for _sensors in (0, BGT1_GP_HAS_DS18B20):
    (
        GreenPowerQuirkBuilder()
        .applies_to(
            device_id=DeviceID.TemperatureSensor,
            manufacturer_id=BGT1_GP_MANUFACTURER_ID,
            model_id=BGT1_GP_MODEL_ID | _sensors,
        )
        .adds_entity(
            Bgt1Temperature, unique_id_suffix="temperature", fallback_name="Temperature"
        )
        .adds_entity(Bgt1Battery, unique_id_suffix="battery", fallback_name="Battery")
        .adds_entity(
            Bgt1BatteryVoltage,
            unique_id_suffix="battery_voltage",
            fallback_name="Battery voltage",
        )
        .friendly_name(manufacturer="EfektaLab", model="BGT1 GP")
        .add_to_registry()
    )
