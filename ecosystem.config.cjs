module.exports = {
  apps: [
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
