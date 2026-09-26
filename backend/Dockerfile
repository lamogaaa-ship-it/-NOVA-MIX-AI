FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY nova_backend ./nova_backend
# Secrets come from the platform's secret store at run time:
#   ANTHROPIC_API_KEY, NOVA_CLIENT_TOKEN_SHA256
ENV NOVA_DATA_DIR=/data
RUN mkdir -p /data && chown 65534:65534 /data
VOLUME /data
EXPOSE 8080
USER 65534
CMD ["python", "-m", "nova_backend", "--port", "8080"]
