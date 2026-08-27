FROM node:24-alpine AS builder
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

FROM nginx:1.29-alpine
RUN apk add --no-cache openssl
COPY deployment/nginx/default.conf /etc/nginx/conf.d/default.conf
COPY deployment/nginx/entrypoint.sh /docker-entrypoint.d/10-local-certificate.sh
COPY --from=builder /app/dist /usr/share/nginx/html
EXPOSE 80 443
