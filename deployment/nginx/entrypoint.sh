#!/bin/sh
set -eu

runtime_cert_dir=/run/nginx-certs
mounted_cert_dir=/etc/nginx/certs

mkdir -p "$runtime_cert_dir"

if [ -s "$mounted_cert_dir/fullchain.pem" ] && [ -s "$mounted_cert_dir/privkey.pem" ]; then
    ln -sf "$mounted_cert_dir/fullchain.pem" "$runtime_cert_dir/fullchain.pem"
    ln -sf "$mounted_cert_dir/privkey.pem" "$runtime_cert_dir/privkey.pem"
else
    openssl req -x509 -nodes -newkey rsa:2048 \
        -keyout "$runtime_cert_dir/privkey.pem" \
        -out "$runtime_cert_dir/fullchain.pem" \
        -days 7 \
        -subj "/CN=localhost" >/dev/null 2>&1
fi
