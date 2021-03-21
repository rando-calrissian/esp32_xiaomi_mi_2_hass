import datetime
import paho.mqtt.client as mqtt
import json
import logging
import logging.config
import yaml

from fit import FitEncoder_Weight
from garmin import GarminConnect
from body_metrics import bodyMetrics


CONFIG = 'config.yml'
LOGGER_CONFIG = 'logger.yml'
LOGGER_NAME = 'garmin_connector'

# Set up global logger
with open(LOGGER_CONFIG, 'r') as fp:
    loggerConfig = yaml.safe_load(fp)
logging.config.dictConfig(loggerConfig)
global logger
logger = logging.getLogger(LOGGER_NAME)

# Read general config
global config
with open(CONFIG, 'r') as fp:
    config = yaml.safe_load(fp)


def main():

    # Prepare all MQTT stuff
    mqttClient = mqtt.Client()
    mqttClient.on_connect = mqtt_on_connect
    mqttClient.on_message = mqtt_on_message
    mqttClient.username_pw_set(
        config['mqtt']['username'], password=config['mqtt']['password'])
    mqttClient.enable_logger(logger=logger)
    mqttClient.connect(config['mqtt']['host'], config['mqtt']['port'], 60)

    # And start the MQTT loop!
    mqttClient.loop_forever()


def mqtt_on_connect(client, userdata, flags, rc):
    logger.info(f'Connected to MQTT brokerr, result code {rc}')
    client.subscribe(config['mqtt']['topic'])


def mqtt_on_message(client, userdata, msg):
    logger.info(f'Received payload: {msg.payload}')
    data = json.loads(msg.payload)

    # Compute data
    metrics = bodyMetrics(
        weight=float(data['Weight']),
        height=float(config['garmin']['height']),
        age=float((datetime.date.today() - config['garmin']['dateOfBirth']).days / 365.25),
        sex=config['garmin']['sex'],
        impedance=float(data['Impedance'])
    )
    dtFormat = '%Y %m %d %H %M %S'
    dt = datetime.datetime.strptime(data['Timestamp'], dtFormat)

    # Prepare Garmin data object
    fit = FitEncoder_Weight()
    fit.write_file_info()
    fit.write_file_creator()
    fit.write_device_info(timestamp=dt)
    fit.write_weight_scale(
        timestamp=dt,
        weight=metrics.weight,
        bmi=metrics.getBMI(),
        percent_fat=metrics.getFatPercentage(),
        percent_hydration=metrics.getWaterPercentage(),
        visceral_fat_mass=metrics.getVisceralFat(),
        bone_mass=metrics.getBoneMass(),
        muscle_mass=metrics.getMuscleMass(),
        basal_met=metrics.getBMR(),
    )
    fit.finish()

    garmin = GarminConnect()
    garminSession = garmin.login(config['garmin']['username'], config['garmin']['password'])
    req = garmin.upload_file(fit.getvalue(), garminSession)
    if req:
        logger.info('Upload to Garmin succeeded')
    else:
        logger.info('Upload to Garmin failed')

if __name__ == '__main__':
    main()
