import json
import boto3

client = boto3.client('iot-data', region_name='us-west-1')

def lambda_handler(event, context):
    print(event)
    response = client.publish(
        topic='bedroom_blinds',
        qos=1,
        payload=json.dumps("open")
    )
    print(response)
    
    return {
        "version": "1.0",
        "response": {
            "shouldEndSession": True
        }
    }
