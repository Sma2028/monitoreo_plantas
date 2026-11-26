@echo off
REM ==== Publica la pagina en GitHub Pages ====
REM Antes: crea en github.com un repositorio PUBLICO llamado "planta" (vacio).
REM Cambia TU_USUARIO por tu usuario de GitHub y haz doble clic en este archivo.
set USUARIO=TU_USUARIO
set REPO=planta

cd /d "%~dp0"
git init
git add index.html
git commit -m "Tablero MQTT de la planta"
git branch -M main
git remote remove origin 2>nul
git remote add origin https://github.com/%USUARIO%/%REPO%.git
git push -u origin main
echo.
echo Listo. Ahora en GitHub: Settings ^> Pages ^> Branch: main / (root) ^> Save
echo Tu link sera: https://%USUARIO%.github.io/%REPO%/
pause
