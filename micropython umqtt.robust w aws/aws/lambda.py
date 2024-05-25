import json
import boto3

client = boto3.client('iot-data', region_name='us-west-1')

def lambda_handler(event, context):
    print(event)
    intent_name = event['request']['intent']['name']
    command = ""
    if intent_name == 'OpenBlindsIntent':
        command = "open"
    elif intent_name == 'CloseBlindsIntent':
        command = "close"
    elif intent_name == 'ForceOpenBlindsIntent':
        command = "force_open"
    elif intent_name == 'ForceCloseBlindsIntent':
        command = "force_close"
    response = client.publish(
        topic='bedroom_blinds',
        qos=1,
        payload=json.dumps(command)
    )
    print(response)
    return {
        "version": "1.0",
        "response": {
            "shouldEndSession": True
        }
    }
