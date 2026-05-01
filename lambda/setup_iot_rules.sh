#!/usr/bin/env bash
# Setup IoT Rules cho cascade architecture:
#   ecg/result → ECG_to_DynamoDB         (luu moi event)
#   ecg/raw    → ECG_to_CloudInference   (trigger Lambda)
#   ecg/alert  → ECG_Alert_SNS           (gui email)
set -e

REGION="ap-southeast-1"
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
LAMBDA_ARN="arn:aws:lambda:${REGION}:${ACCOUNT_ID}:function:ecg-inference"
SNS_TOPIC_ARN="arn:aws:sns:${REGION}:${ACCOUNT_ID}:ecg-alerts"

echo "Account: $ACCOUNT_ID"
echo "Lambda:  $LAMBDA_ARN"
echo ""

# ──────────────────────────────────────────────────────────────
# Rule 1: ECG_to_CloudInference (MOI) — ecg/raw → Lambda
# ──────────────────────────────────────────────────────────────
echo "=== [1/2] Tao Rule ECG_to_CloudInference ==="

# Xoa rule cu neu da co (idempotent)
aws iot delete-topic-rule --rule-name ECG_to_CloudInference --region "$REGION" 2>/dev/null || true

aws iot create-topic-rule \
  --region "$REGION" \
  --rule-name ECG_to_CloudInference \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'ecg/raw'\",
    \"description\": \"Trigger cloud Lambda inference khi ESP32 publish raw waveform\",
    \"actions\": [{
      \"lambda\": {\"functionArn\": \"$LAMBDA_ARN\"}
    }],
    \"ruleDisabled\": false,
    \"awsIotSqlVersion\": \"2016-03-23\"
  }"

# Cap quyen IoT goi Lambda (idempotent)
aws lambda remove-permission \
  --region "$REGION" \
  --function-name ecg-inference \
  --statement-id iot-invoke-from-raw 2>/dev/null || true

aws lambda add-permission \
  --region "$REGION" \
  --function-name ecg-inference \
  --statement-id iot-invoke-from-raw \
  --action lambda:InvokeFunction \
  --principal iot.amazonaws.com \
  --source-arn "arn:aws:iot:${REGION}:${ACCOUNT_ID}:rule/ECG_to_CloudInference"

echo "  Rule ECG_to_CloudInference: OK"
echo ""

# ──────────────────────────────────────────────────────────────
# Rule 2: ECG_Alert_SNS (SUA) — doi tu 'ecg/result' sang 'ecg/alert'
# ──────────────────────────────────────────────────────────────
echo "=== [2/2] Cap nhat Rule ECG_Alert_SNS ==="

# AWS IoT khong support 'update' rule, phai delete + create lai
aws iot delete-topic-rule --rule-name ECG_Alert_SNS --region "$REGION" 2>/dev/null || true

aws iot create-topic-rule \
  --region "$REGION" \
  --rule-name ECG_Alert_SNS \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'ecg/alert'\",
    \"description\": \"Gui SNS email khi ca edge va cloud cung phat hien abnormal\",
    \"actions\": [{
      \"sns\": {
        \"targetArn\": \"$SNS_TOPIC_ARN\",
        \"roleArn\": \"arn:aws:iam::${ACCOUNT_ID}:role/aws-iot-rule-sns-role\",
        \"messageFormat\": \"RAW\"
      }
    }],
    \"ruleDisabled\": false,
    \"awsIotSqlVersion\": \"2016-03-23\"
  }"

echo "  Rule ECG_Alert_SNS: OK (now subscribes to 'ecg/alert')"
echo ""

echo "=== DONE ==="
echo ""
echo "Architecture sau khi setup:"
echo "  ESP32 → ecg/result → ECG_to_DynamoDB → DynamoDB"
echo "  ESP32 → ecg/raw    → ECG_to_CloudInference → Lambda → ecg/alert"
echo "                                                            ↓"
echo "                                           ECG_Alert_SNS → Email"
