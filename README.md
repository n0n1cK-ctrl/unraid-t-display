# unraid-t-display

ESP32/LILYGO T-Display project for Unraid server monitoring with a switchable cover view for the latest imported movie or series.

`service/` contains the existing Unraid cover service, recovered from the running installation on 14 September 2026, plus Whisparr integration. Firmware is maintained separately; this change does not flash or modify the display.

## Whisparr (Eros / v3)

In **Settings → Connect → + → Webhook**:

- Name: **LilyGO Cover**
- URL: `http://192.168.178.56:8089/webhook/whisparr/WEBHOOK_TOKEN`
- Replace `WEBHOOK_TOKEN` with the existing value in the service's private `.env`.
- Method: **POST**.
- Enable **On Import** (`onDownload`) and **On Upgrade**; leave other events disabled and tags empty.
- Run **Test**, then save. A test returns HTTP 200 and does not replace the cover.

Add this read-only Docker mount to the existing `unraid-cover` container:

```text
/mnt/user/appdata/whisparr/MediaCover/movie:/posters/whisparr:ro
```

The `movie` subdirectory is intentional: verified with Whisparr **3.5.0.1585, eros**. Both scenes and movies use `movie.id` in `eventType: Download` webhooks. Whisparr reuses Radarr's payload parsing, while the source remains `whisparr`, so identical numeric IDs in different Arr instances cannot select each other's posters. Whisparr v2 (`series` payload) is not covered by this setup.

Minimal synthetic import (use only an isolated test service unless replacing the live cover is intended):

```json
{"eventType":"Download","isUpgrade":false,"movie":{"id":7,"title":"Synthetic import","itemType":"scene"}}
```

## Existing behavior

- Sonarr: `/webhook/sonarr/WEBHOOK_TOKEN`, `series.id`.
- Radarr: `/webhook/radarr/WEBHOOK_TOKEN`, `movie.id`.
- Whisparr: `/webhook/whisparr/WEBHOOK_TOKEN`, `movie.id`.
- Only `Download` events enqueue work. Test, Grab, Rename and other events leave the latest import untouched. Successful persistence returns HTTP 202; invalid import IDs return 400.
- Posters are read from `/posters/<source>/<id>/poster.jpg`. Sonarr/Radarr mounts and their processing are unchanged in production.
- SQLite `/data/state.sqlite` persists the newest received import and prepared frame. New imports replace pending older ones; delivery retries every ten seconds. Missing posters keep delivery pending.
- The service crops to 170 × 320 RGB565LE, checks the device's `GET /info`, and sends `POST /cover` with a SHA-256 digest. `/health` checks the service itself, not delivery.
- No Arr API access or API keys are needed during normal operation. Display and webhook secrets belong only in `.env`; never commit them.

## Run / test

Adapt paths in `service/compose.yaml` to the actual installation. Copy `.env.example` to `.env` and populate the existing secrets. Preserve the `/data` volume on updates. The Compose example uses service name `display-cover`; the existing installation uses a manually created container named `unraid-cover`. Do not start a second container on port 8089 alongside it.

```sh
cd service
python -m pip install -r requirements.txt
python -m unittest discover -v
# For a new Compose-based installation:
docker compose up -d --build
```

Fourteen tests cover the original service behavior plus Whisparr movie/scene imports, upgrades, event filtering, invalid IDs, authenticated HTTP webhooks, source isolation, cross-source latest state, restart persistence and retry after a missing poster. Tests use temporary SQLite/poster directories and a simulated display; they never replace the real cover.

## Deployment verification — 14 September 2026

- Baseline copied from the running container and matched to `/mnt/user/appdata/unraid-cover/app.py` by SHA-256: `7b3c92803b9e22fb8f7729eaea5f0636a9e8ba0a141a1c19319ea7d37d9e679c`.
- Sonarr notification ID 13 and Radarr ID 12: `LilyGO Cover`, imports/upgrades enabled; configuration not changed.
- All 14 tests passed locally and in an isolated container on Unraid before deployment.
- An existing Whisparr poster converted to exactly 108800 bytes without uploading it.
- Deployed image: `unraid-cover:whisparr-20260914`; container `unraid-cover`, port `192.168.178.56:8089`, restart policy `unless-stopped`.
- Private configuration/code backup: `/mnt/user/appdata/unraid-cover/backup-before-whisparr-20260914/`; SQLite backup: `data/before-whisparr-20260914.sqlite`.
- Previous container retained stopped as `unraid-cover-before-whisparr-20260914`. For rollback, stop and rename the new container before renaming/starting the previous one. Do not overwrite the live database with the backup unless intentionally reverting newer imports.
- After deployment, harmless HTTP Test webhooks to all three production routes returned 200.
- Whisparr `LilyGO Cover` notification ID 2 created and verified with imports/upgrades enabled. Whisparr's own connection test returned 200 before creation (201) and preserved the latest cover. Existing Whisparr notifications were verified unchanged.
- Recovered the missed real Whisparr import from 18:23 CEST on 14 September after verifying its import history and available movie file. The cover webhook returned 202; SQLite marked the Whisparr frame delivered, and the real display's `/info` SHA-256 exactly matched the converted poster. Future imports are now connected automatically.

Payload source: [Whisparr WebhookBase](https://github.com/Whisparr/Whisparr-Eros/blob/eros-develop/src/NzbDrone.Core/Notifications/Webhook/WebhookBase.cs), especially `BuildOnDownloadPayload`.
