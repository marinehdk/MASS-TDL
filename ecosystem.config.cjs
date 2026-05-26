module.exports = {
  apps: [
    {
      name: 'sil-backend',
      script: 'python3',
      args: '-m uvicorn sil_orchestrator.main:app --port 8000 --host 127.0.0.1',
      interpreter: 'none',
      cwd: __dirname,
      env: {
        PYTHONPATH: 'src',
      },
      autorestart: true,
      watch: false,
    },
    {
      name: 'sil-frontend',
      script: 'npm',
      args: 'run dev --prefix web',
      interpreter: 'none',
      cwd: __dirname,
      autorestart: true,
      watch: false,
    }
  ]
};
