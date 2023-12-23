#!/usr/bin/env python3

""" MQTT exporter """

import json
#from msilib.schema import ServiceInstall
import re
import signal
import logging
import os
import sys
import serial

import paho.mqtt.client as mqtt
from prometheus_client import Counter, Gauge, start_http_server

temperature_c_server = 0
temperature_c_sensor = 1
temperature_f_server = 2
temperature_f_sensor = 3
flag_server = 4
flag_sensor = 5

logging.basicConfig(filename='register.log', level=logging.DEBUG)
LOG = logging.getLogger("[mqtt-exporter]")

########################################################################
# Prometheus
########################################################################

prom_msg_counter = None
prom_temp_c_server_gauge = None
prom_temp_c_sensor_gauge = None
prom_temp_f_server_gauge = None
prom_temp_f_sensor_gauge = None
prom_flag_server_gauge = None
prom_flag_sensor_gauge = None


def create_msg_counter_metrics():
    global prom_msg_counter

    prom_msg_counter = Counter( 'number_msgs',
        'Number of received messages'
    )

def create_temp_gauge_c_server_metrics():
    global prom_temp_c_server_gauge

    prom_temp_c_server_gauge = Gauge( 'temp_c_server',
        'Temperature Server [Celsius Degrees]'
    )

def create_temp_gauge_c_sensor_metrics():
    global prom_temp_c_sensor_gauge

    prom_temp_c_sensor_gauge = Gauge( 'temp_c_sensor',
        'Temperature Sensor [Celsius Degrees]'
    )

def create_temp_gauge_f_server_metrics():
    global prom_temp_f_server_gauge

    prom_temp_f_server_gauge = Gauge( 'temp_f_server',
        'Temperature Server [Farenheit Degrees]'
    )

def create_temp_gauge_f_sensor_metrics():
    global prom_temp_f_sensor_gauge

    prom_temp_f_sensor_gauge = Gauge( 'temp_f_sensor',
        'Temperature Sensor [Farenheit Degrees]'
    )

def create_temp_gauge_flag_server_metrics():
    global prom_flag_server_gauge

    prom_flag_server_gauge = Gauge( 'flag_server',
        'Flag alarm Server [1->off; 2->on]'
    )

def create_temp_gauge_flag_sensor_metrics():
    global prom_flag_sensor_gauge

    prom_flag_sensor_gauge = Gauge( 'flag_sensor',
        'Flag alarm Sensor [1->off; 2->on]'
    )


def parse_message(raw_topic, raw_payload):
    try:
        payload = json.loads(raw_payload)
    except json.JSONDecodeError:
        LOG.error(" Failed to parse payload as JSON: %s", str(payload, 'ascii'))
        print(" Failed to parse payload as JSON: {0:s}".format(str(payload, 'ascii')))
        return None, None
    except UnicodeDecodeError:
        LOG.error(" Encountered undecodable payload: %s", raw_payload)
        print(" Encountered undecodable payload: {0:s}".format(raw_payload))
        return None, None
    
    topic = raw_topic

    return topic, payload

def parse_metric(data):
    if isinstance(data, (int,float)):
        return data
    
    if isinstance(data, bytes):
        data = data.decode()

    if isinstance(data, str):
        data = data.upper()

    return float(data)

def parse_metrics(data, topic, client_id):
    for metric, value in data.items():
        if isinstance(value, dict):
            LOG.debug(" Parsing dict %s, %s", metric, value)
            print(" Parsing dict {0:s}, {1:s}".format(metric, value))
            parse_metrics(value, topic, client_id)
            continue

        try:
            metric_value = parse_metric(value)

        except ValueError as err:
            LOG.error(" Failed to convert %s, Error: %s", metric, err)
            print(" Failed to convert {0:s}, Error: {1:s}".format(metric, err))

########################################################################
# MQTT
########################################################################
def on_connect(client, _, __, rc):
    LOG.info(" Connected with result code: %s", rc)
    print(" Connected with result code: {0:d}".format(rc))
    
    client.subscribe("temp_c_server")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))    
    client.subscribe("temp_c_sensor")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))    
    client.subscribe("temp_f_server")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))    
    client.subscribe("temp_f_sensor")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))    
    client.subscribe("flag_server")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))
    client.subscribe("flag_sensor")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)
        print("[ERROR]: MQTT {0:d}".format(rc))

def on_message(_, userdata, msg):
    LOG.info(" [Topic: %s] %s", msg.topic, msg.payload)
    print(" [Topic: {0:s}] {1:s}".format(str(msg.topic), str(msg.payload)))

    topic, payload = parse_message(msg.topic, msg.payload)
    LOG.debug(" \t Topic: %s", topic)
    print(" \t Topic: {0:s}".format(str(topic)))

    LOG.debug(" \t Payload: %s", payload)
    print(" \t Payload: {0:s}".format(str(payload)))

    if not topic or not payload:
        LOG.error(" [ERROR]: Topic or Payload not found")
        print(" [ERROR]: Topic or Payload not found")

        return

    prom_msg_counter.inc()
    if(msg.topic == "temp_c_server"):
        prom_temp_c_server_gauge.set(payload)
    if(msg.topic == "temp_c_sensor"):
        prom_temp_c_sensor_gauge.set(payload)
    if(msg.topic == "temp_f_server"):
        prom_temp_f_server_gauge.set(payload)
    if(msg.topic == "temp_f_sensor"):
        prom_temp_f_sensor_gauge.set(payload)
    if(msg.topic == "flag_server"):
        prom_flag_server_gauge.set(payload)
    if(msg.topic == "flag_sensor"):
        prom_flag_sensor_gauge.set(payload)

########################################################################
# Main
########################################################################
def main():
    # Create MQTT client
    client = mqtt.Client()

    def stop_reques(signum, frame):
        LOG.debug(" Stopping MQTT exporter")
        print(" Stopping MQTT exporter")

        client.disconnect()
        ser.close()
        sys.exit(0)

    #Serial port
    try:
        ser = serial.Serial(port="/dev/ttyACM0",
                            baudrate=115200,
                            parity=serial.PARITY_NONE,
                            stopbits=serial.STOPBITS_ONE,
                            bytesize=serial.EIGHTBITS,
                            timeout=2000)
        ser.flushInput()
        ser.flush()
        ser.isOpen()
        LOG.info("Serial Port /dev/ACM0 is opened")
        print("Serial Port /dev/ACM0 is opened")
    except IOError:
        LOG.error("serial port is already opened or does not exist")
        print("serial port is already opened or does not exist")
        sys.exit(0)

    # Create Prometheus metrics
    create_msg_counter_metrics()
    create_temp_gauge_c_server_metrics()
    create_temp_gauge_c_sensor_metrics()
    create_temp_gauge_f_server_metrics()
    create_temp_gauge_f_sensor_metrics()
    create_temp_gauge_flag_server_metrics()
    create_temp_gauge_flag_sensor_metrics()

    # Start prometheus server
    start_http_server(9000)

    # Configure MQTT topic
    client.on_connect = on_connect
    client.on_message = on_message
    # Suscribe MQTT topics
    LOG.debug(" Connecting to localhost")
    # Connect to MQTT broker
    client.connect("localhost", 1883, 60)
    # Waiting for messages
    client.loop_start()

    while True:
        # Reading from serial port
        line = ser.readline()
        # Print data received
        LOG.debug("Serial Data: %s", str(line, 'ascii').rstrip())
        print("Serial Data: {0:s}".format(str(line, 'ascii').rstrip()))
        # Get data
        csv_fields=line.rstrip()
        fields=csv_fields.split(b'\x3B')   # codigo ascii del ; en hexadecimal
        
        # debug
        index=0
        for value in fields:
            LOG.debug("Field[%d]: %f", index, float(value))
            print("Field[{0:d}]: {1:f}".format(index, float(value)))
            index = index + 1
        
        print(fields)
        # Publish data on corresponding topic
        if(fields[1] == temperature_c_server):
            client.publish(topic="temp_c_server", payload=fields[0], qos=0, retain=False)
        if(fields[1] == temperature_c_sensor):
            client.publish(topic="temp_c_sensor", payload=fields[0], qos=0, retain=False)
        if(fields[1] == temperature_f_server):
            client.publish(topic="temp_f_server", payload=fields[0], qos=0, retain=False)
        if(fields[1] == temperature_f_sensor):
            client.publish(topic="temp_f_sensor", payload=fields[0], qos=0, retain=False)
        if(fields[1] == flag_server):
            client.publish(topic="flag_server", payload=fields[0], qos=0, retain=False)
        if(fields[1] == flag_sensor):
            client.publish(topic="flag_sensor", payload=fields[0], qos=0, retain=False)


########################################################################
# Main
########################################################################
if __name__ == "__main__":
    main()