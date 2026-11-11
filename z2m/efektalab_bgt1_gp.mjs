// Zigbee2MQTT external converter for the EfektaLab BGT1 running the
// flower-cc2530-gp Green Power firmware (BOARD=bgt1).
//
// zigbee-herdsman names a Green Power device after its GPD DeviceID only:
// BGT1 commissions as TemperatureSensor 0x30, so the model is
// "GreenPower_48". The manufacturer/model IDs from the commissioning
// application information are not used by zigbee-herdsman.
//
// The firmware sends Multi-Cluster Reporting (0xA2):
//   0x0402/0x0000 int16  temperature, 0.01 C, 0x8000 invalid
//   0x0001/0x0020 uint8  battery voltage, 100 mV, 0xFF invalid
//   0x0001/0x0021 uint8  battery remaining, 0.5 %, 0xFF invalid
import * as exposes from "zigbee-herdsman-converters/lib/exposes";
import {hasAlreadyProcessedMessage} from "zigbee-herdsman-converters/lib/utils";

const e = exposes.presets;

const MULTI_CLUSTER_REPORTING = 0xa2;

function value(reports, clusterID, attributeID, attributeName) {
    const report = reports.find(
        (r) => r.clusterID === clusterID && (r.attribute === attributeID || r.attribute === attributeName),
    );
    return report?.attrData;
}

const fzReport = {
    cluster: "greenPower",
    type: ["commandNotification"],
    convert: (model, msg, publish, options, meta) => {
        if (msg.data.commandID !== MULTI_CLUSTER_REPORTING) return;
        const reports = msg.data.commandFrame?.reports;
        if (!Array.isArray(reports)) return;
        if (hasAlreadyProcessedMessage(msg, model, msg.data.frameCounter, `${msg.device.ieeeAddr}_${msg.data.commandID}`)) return;

        const result = {};
        const temperature = value(reports, 0x0402, 0x0000, "measuredValue");
        if (typeof temperature === "number" && temperature !== -0x8000) {
            result.temperature = temperature / 100;
        }
        const voltage = value(reports, 0x0001, 0x0020, "batteryVoltage");
        if (typeof voltage === "number" && voltage !== 0xff) {
            result.voltage = voltage * 100;
        }
        const percent = value(reports, 0x0001, 0x0021, "batteryPercentageRemaining");
        if (typeof percent === "number" && percent <= 200) {
            result.battery = percent / 2;
        }
        return result;
    },
};

export default {
    fingerprint: [{modelID: "GreenPower_48"}],
    model: "BGT1-GP",
    vendor: "EfektaLab",
    description: "BGT1 temperature sensor (DS18B20) with flower-cc2530-gp Green Power firmware",
    fromZigbee: [fzReport],
    toZigbee: [],
    exposes: [e.temperature(), e.battery(), e.battery_voltage()],
};
