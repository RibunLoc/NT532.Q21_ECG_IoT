#!/usr/bin/env bash
# Build Lambda Layer cho onnxruntime + numpy, dong goi handler.
# Yeu cau: Docker (de build wheel khop voi Lambda Linux env)
set -e

REGION="ap-southeast-1"
LAYER_NAME="ecg-onnx-runtime"
FUNCTION_NAME="ecg-inference"
S3_BUCKET="ecg-lambda-deploy-123"
S3_KEY_LAYER="layers/onnx_layer.zip"

echo "=== [1/4] Build ONNX Runtime layer trong Docker ==="
rm -rf layer_build
mkdir -p layer_build/python

MSYS_NO_PATHCONV=1 docker run --rm \
  --entrypoint /var/lang/bin/pip \
  -v "$PWD":/var/task \
  public.ecr.aws/lambda/python:3.11 \
  install -r /var/task/requirements.txt -t /var/task/layer_build/python --no-cache-dir

# Strip files khong can de giam size layer
echo "  Stripping unused files..."
find layer_build/python -type d -name "__pycache__"        -exec rm -rf {} + 2>/dev/null || true
find layer_build/python -type d -name "tests"              -exec rm -rf {} + 2>/dev/null || true
find layer_build/python -type d -name "*.dist-info"        -exec rm -rf {} + 2>/dev/null || true
find layer_build/python -type f -name "*.pyc"              -delete 2>/dev/null || true
find layer_build/python -type f -name "*.pyo"              -delete 2>/dev/null || true
# Strip ONNX Runtime training providers (chi can inference)
rm -rf layer_build/python/onnxruntime/transformers 2>/dev/null || true
rm -rf layer_build/python/onnxruntime/training     2>/dev/null || true

cd layer_build
zip -r9 ../onnx_layer.zip python > /dev/null
cd ..
echo "  Layer size: $(du -h onnx_layer.zip | cut -f1)"

echo ""
echo "=== [2/4] Upload layer len S3 + Publish ==="
# Tao bucket neu chua co (idempotent — bo qua loi neu da ton tai)
aws s3api create-bucket \
  --region "$REGION" \
  --bucket "$S3_BUCKET" \
  --create-bucket-configuration LocationConstraint="$REGION" \
  2>/dev/null || true

aws s3 cp onnx_layer.zip "s3://$S3_BUCKET/$S3_KEY_LAYER"

LAYER_VERSION_ARN=$(aws lambda publish-layer-version \
  --region "$REGION" \
  --layer-name "$LAYER_NAME" \
  --description "ONNX Runtime + numpy cho ECG inference" \
  --content "S3Bucket=$S3_BUCKET,S3Key=$S3_KEY_LAYER" \
  --compatible-runtimes python3.11 \
  --query 'LayerVersionArn' --output text)
echo "  Layer ARN: $LAYER_VERSION_ARN"

echo ""
echo "=== [3/4] Dong goi handler + model ==="
rm -f function.zip
zip -j function.zip handler.py ecg_cloud_model.onnx
echo "  Function size: $(du -h function.zip | cut -f1)"

echo ""
echo "=== [4/4] Update Lambda function code + attach layer ==="
aws lambda update-function-code \
  --region "$REGION" \
  --function-name "$FUNCTION_NAME" \
  --zip-file fileb://function.zip > /dev/null

aws lambda update-function-configuration \
  --region "$REGION" \
  --function-name "$FUNCTION_NAME" \
  --layers "$LAYER_VERSION_ARN" \
  --timeout 30 \
  --memory-size 1024 > /dev/null

echo ""
echo "=== DONE ==="
echo "Function: $FUNCTION_NAME"
echo "Layer:    $LAYER_VERSION_ARN"
