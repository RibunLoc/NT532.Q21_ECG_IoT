# ECG Lambda Deploy — Step by Step

Lam tuan tu tu Step 1 → Step 8. Moi step deu copy-paste duoc.

---

## **Prerequisites**

Truoc khi bat dau, dam bao co:
- [ ] AWS CLI da config (`aws configure` hoac SSO)
- [ ] Docker Desktop dang chay (de build ONNX layer khop Linux)
- [ ] File `ecg_cloud_model.onnx` da copy vao thu muc nay
- [ ] Bash shell (Git Bash tren Windows duoc)

Test nhanh:
```bash
aws sts get-caller-identity   # phai tra ve account info
docker --version              # phai tra ve version
ls ecg_cloud_model.onnx       # phai ton tai
```

---

## **Step 1: Tao SNS Topic + Subscribe Email**

```bash
# Tao topic
aws sns create-topic \
  --region ap-southeast-1 \
  --name ecg-alerts

# Output: "TopicArn": "arn:aws:sns:ap-southeast-1:XXXXXX:ecg-alerts"
# COPY TopicArn ra, can cho Step 4
```

Subscribe email cua ban:
```bash
aws sns subscribe \
  --region ap-southeast-1 \
  --topic-arn "arn:aws:sns:ap-southeast-1:XXXXXX:ecg-alerts" \
  --protocol email \
  --notification-endpoint hothanhloc12345@gmail.com
```

→ **Mo email vao confirm subscription** (link "Confirm subscription" tu AWS Notifications)

---

## **Step 2: Tao DynamoDB Table**

```bash
aws dynamodb create-table \
  --region ap-southeast-1 \
  --table-name ecg-events \
  --attribute-definitions \
      AttributeName=device_id,AttributeType=S \
      AttributeName=timestamp,AttributeType=N \
  --key-schema \
      AttributeName=device_id,KeyType=HASH \
      AttributeName=timestamp,KeyType=RANGE \
  --billing-mode PAY_PER_REQUEST
```

Doi ~30s cho table ACTIVE:
```bash
aws dynamodb wait table-exists --region ap-southeast-1 --table-name ecg-events
echo "Table ready"
```

---

## **Step 3: Tao IAM Role cho Lambda**

Tao file trust policy:
```bash
cat > trust-policy.json <<EOF
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Principal": {"Service": "lambda.amazonaws.com"},
    "Action": "sts:AssumeRole"
  }]
}
EOF
```

Tao role:
```bash
aws iam create-role \
  --role-name ecg-lambda-role \
  --assume-role-policy-document file://trust-policy.json
```

Attach policies (logs + DynamoDB + SNS):
```bash
aws iam attach-role-policy \
  --role-name ecg-lambda-role \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole

aws iam attach-role-policy \
  --role-name ecg-lambda-role \
  --policy-arn arn:aws:iam::aws:policy/AmazonDynamoDBFullAccess

aws iam attach-role-policy \
  --role-name ecg-lambda-role \
  --policy-arn arn:aws:iam::aws:policy/AmazonSNSFullAccess

# Quan trong: Lambda publish MQTT topic 'ecg/alert' (iot_data.publish trong
# handler.py) nen can quyen iot:Publish. Khong co quyen nay → alert that bai.
aws iam attach-role-policy \
  --role-name ecg-lambda-role \
  --policy-arn arn:aws:iam::aws:policy/AWSIoTDataAccess
```

Lay Role ARN:
```bash
aws iam get-role --role-name ecg-lambda-role --query 'Role.Arn' --output text
# COPY arn:aws:iam::XXXXXX:role/ecg-lambda-role
```

---

## **Step 4: Tao Lambda Function (lan dau)**

Dong goi tam (rong) de tao function:
```bash
echo 'def lambda_handler(e,c): return {"status":"placeholder"}' > _tmp.py
zip _tmp.zip _tmp.py

aws lambda create-function \
  --region ap-southeast-1 \
  --function-name ecg-inference \
  --runtime python3.11 \
  --role "arn:aws:iam::XXXXXX:role/ecg-lambda-role" \
  --handler handler.lambda_handler \
  --timeout 30 \
  --memory-size 1024 \
  --environment "Variables={DDB_TABLE=ecg-events,ALERT_TOPIC=ecg/alert,VEB_THRESHOLD=0.3}" \
  --zip-file fileb://_tmp.zip

rm _tmp.py _tmp.zip
```

> Thay `XXXXXX` bang AWS account ID cua ban (lay tu Step 1 va 3).
>
> **Luu y env var (khop voi handler.py):**
> - `ALERT_TOPIC=ecg/alert` — handler doc bien nay (KHONG phai SNS_TOPIC_ARN).
>   Lambda KHONG goi SNS truc tiep, ma publish MQTT topic `ecg/alert` →
>   IoT Rule rieng (Step 7b) moi trigger SNS.
> - `VEB_THRESHOLD=0.3` — sau khi train model moi, 0.3 cho VEB sens 79.81% +
>   PPV 81.72% (tot hon 0.4). Doi neu muon nhay/dac hieu hon.

---

## **Step 5: Build + Deploy code that su (chay 1 lenh)**

Make script executable:
```bash
chmod +x deploy.sh
```

Chay:
```bash
./deploy.sh
```

Script se:
1. Build ONNX layer trong Docker (~2 phut lan dau, sau cache nhanh)
2. Publish layer len AWS
3. Zip `handler.py` + `ecg_cloud_model.onnx`
4. Update function code + attach layer

Output mong doi:
```
=== [1/4] Build ONNX Runtime layer trong Docker ===
  Layer size: 28M
=== [2/4] Publish layer len AWS ===
  Layer ARN: arn:aws:lambda:ap-southeast-1:XXXXXX:layer:ecg-onnx-runtime:1
=== [3/4] Dong goi handler + model ===
  Function size: 920K
=== [4/4] Update Lambda function code + attach layer ===
=== DONE ===
```

---

## **Step 6: Test Lambda voi sample event**

Tao test payload (mock 1 beat Normal):
```bash
python -c "
import json, random
samples = [random.uniform(-1, 1) for _ in range(187)]
print(json.dumps({
  'device_id': 'ecg-device-001',
  'timestamp': 1745923456,
  'edge_label': 'V',
  'edge_confidence': 0.92,
  'samples': samples
}))" > test_event.json
```

Invoke Lambda:
```bash
aws lambda invoke \
  --region ap-southeast-1 \
  --function-name ecg-inference \
  --payload fileb://test_event.json \
  --cli-binary-format raw-in-base64-out \
  response.json

cat response.json
```

Output mong doi:
```json
{"status":"ok","device_id":"ecg-device-001","cloud_label":"N","cloud_conf":0.4521,"is_alert":false}
```

(Random data nen label co the ra bat ky lop nao, mien la khong loi.)

---

## **Step 7: Tao IoT Rule de tu dong trigger Lambda**

Lay Lambda ARN:
```bash
LAMBDA_ARN=$(aws lambda get-function \
  --region ap-southeast-1 \
  --function-name ecg-inference \
  --query 'Configuration.FunctionArn' --output text)
echo $LAMBDA_ARN
```

Tao IoT Rule:
```bash
aws iot create-topic-rule \
  --region ap-southeast-1 \
  --rule-name ecg_waveform_to_lambda \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'ecg/raw'\",
    \"description\": \"Forward ECG raw waveform to inference Lambda\",
    \"actions\": [{
      \"lambda\": {\"functionArn\": \"$LAMBDA_ARN\"}
    }],
    \"ruleDisabled\": false,
    \"awsIotSqlVersion\": \"2016-03-23\"
  }"
```

Cap quyen cho IoT goi Lambda:
```bash
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)

aws lambda add-permission \
  --region ap-southeast-1 \
  --function-name ecg-inference \
  --statement-id iot-invoke \
  --action lambda:InvokeFunction \
  --principal iot.amazonaws.com \
  --source-arn "arn:aws:iot:ap-southeast-1:$ACCOUNT_ID:rule/ecg_waveform_to_lambda"
```

---

## **Step 7b: Tao IoT Rule `ecg/alert` → SNS Email**

> Lambda KHONG goi SNS truc tiep. Khi phat hien bat thuong, no publish MQTT
> topic `ecg/alert`. Rule nay subscribe `ecg/alert` va day sang SNS → email.
> THIEU rule nay thi se KHONG co email du Lambda chay dung.

```bash
SNS_ARN="arn:aws:sns:ap-southeast-1:XXXXXX:ecg-alerts"   # tu Step 1

aws iot create-topic-rule \
  --region ap-southeast-1 \
  --rule-name ecg_alert_to_sns \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'ecg/alert'\",
    \"description\": \"Forward ECG alert to SNS email\",
    \"actions\": [{
      \"sns\": {
        \"targetArn\": \"$SNS_ARN\",
        \"roleArn\": \"arn:aws:iam::XXXXXX:role/ecg-lambda-role\",
        \"messageFormat\": \"RAW\"
      }
    }],
    \"ruleDisabled\": false,
    \"awsIotSqlVersion\": \"2016-03-23\"
  }"
```

> `roleArn`: IoT can role co quyen sns:Publish. Tai dung ecg-lambda-role
> (da co AmazonSNSFullAccess), nhung role nay phai cho phep iot.amazonaws.com
> assume. Neu loi, tao role rieng cho IoT→SNS hoac them iot vao trust policy.

---

## **Step 8: End-to-end test qua MQTT**

Mo AWS Console → IoT Core → MQTT Test Client → Publish to topic:

**Topic:** `ecg/raw`

**Payload:** copy noi dung `test_event.json` o Step 6

Sau khi publish:
1. CloudWatch Logs → `/aws/lambda/ecg-inference` → xem invocation
2. DynamoDB Console → table `ecg-events` → kiem tra item moi
3. Neu cloud_label='V' → check email inbox cho alert

---

## **Troubleshooting**

| Loi | Nguyen nhan | Fix |
|-----|-------------|-----|
| `Unable to import module 'handler'` | Thieu layer | Chay lai Step 5 |
| `An error occurred (AccessDenied) when calling Publish` | IAM role thieu SNSPublish | Chay lai Step 3 attach SNS policy |
| `ResourceNotFoundException` (DynamoDB) | Table chua tao | Chay Step 2 |
| Docker build fail | Docker Desktop chua chay | Khoi dong Docker Desktop |
| Lambda timeout | Cold start qua lau | Tang `--timeout 60` va `--memory-size 1536` |

---

## **Tom tat luong sau khi xong:**

```
ESP32 ── publish ──> ecg/raw ──(gateway forward)──> AWS IoT Core
                                                     │
                                       IoT Rule: SELECT * FROM 'ecg/raw'
                                                     │
                                                     ▼
                                          Lambda ecg-inference
                                                     │
                                  ┌──────────────────┼──────────────────┐
                                  ▼                  ▼                   ▼
                             DynamoDB           ONNX run        if alert → publish
                            (ecg-events)        (verify)         MQTT 'ecg/alert'
                                                                        │
                                                          IoT Rule: SELECT * FROM 'ecg/alert'
                                                                        │
                                                                        ▼
                                                                   SNS → Email
```

Sau Step 8 chay OK → san sang code ESP32 (cascade logic publish MQTT).
