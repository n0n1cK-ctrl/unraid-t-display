# LilyGO T-Display for Unraid

Firmware for the LilyGO T-Display-S3 with five views:

- Unraid status
- Storage and RAM
- GPU
- Airflow/fans
- Poster with title and season/episode for series

GPIO14 switches to the next view. GPIO0 refreshes the current view. Wi-Fi credentials and the cover key stay only in the local, ignored `include/secrets.h` file.

## Cover display

The service processes only successful import webhooks (`eventType=Download`) from Radarr and Sonarr. Posters are read from the local MediaCover directories, cropped to 170 × 320 pixels and sent to the display as RGB565LE. The latest state is stored persistently and delivery is retried after temporary failures.

Series webhooks use `episodes[]`; the highest season/episode pair from the import is shown as `SxxExx`.

## Compile and flash

```sh
pio run
pio run -t upload --upload-port <current-port>
```

Find the port with `pio device list`. Before compiling, create a local `firmware/include/secrets.h` from your installation template. This file is never versioned.

## Storage pool note

This installation uses pooled storage without a traditional Unraid parity array. Storage reporting therefore uses the mounted pool dataset rather than the usual array/parity paths. Commands and mount paths are installation-specific and must be adapted and verified for a standard array or another pool configuration.

## Unraid service

The service is located in `service/` and is typically copied to `/mnt/user/appdata/unraid-cover/` on Unraid.

```sh
cd service
python -m unittest discover -v
docker compose up -d --build
```

Webhook endpoints:

- Radarr: `http://<unraid-ip>:8089/webhook/radarr/<webhook-token>`
- Sonarr: `http://<unraid-ip>:8089/webhook/sonarr/<webhook-token>`

Enable **On Import** and **On Upgrade** in Radarr and Sonarr. Keep credentials and tokens only in the local `.env` file.
