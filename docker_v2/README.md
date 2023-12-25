# Instrucciones de uso
## Contenedor "mqtt_exporter"
Este contenedor no ejecuta de por si el script de Python al iniciarse (de ahi viene el bug de que se quede pillado y no reciba nada), para ejecutar el script es necesario realizar los siguientes pasos:
```bash
$ vmu@vm-devel: docker compose exec mqtt_exporter /bin/sh
```
```bash
$ /mqtt-exporter: python3 main.py
```

Si se hacen cambios no sera necesario volver a construir el contenedor o reiniciarlo. Simplemente guardando el archivo y deteniendo (Ctrl+C) y ejecutando el comando de nuevo se cargara la ultima version.
