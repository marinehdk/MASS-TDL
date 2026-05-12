# SIL HMI — React + MapLibre + Zustand + RTK Query
FROM node:20-alpine

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci

COPY . .

EXPOSE 5173

# Dev mode by default (vite hot reload). For prod use `npm run build` + nginx.
CMD ["npx", "vite", "--host", "0.0.0.0", "--port", "5173"]
