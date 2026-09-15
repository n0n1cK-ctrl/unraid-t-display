# LilyGO T-Display für Unraid

Firmware für das LilyGO T-Display-S3 mit fünf Ansichten:

- Unraid-Status
- Speicher und RAM
- GPU
- Airflow/Lüfter
- Poster mit Titel sowie Staffel/Folge bei Serien

Mit GPIO14 wird zur nächsten Ansicht gewechselt. GPIO0 aktualisiert die aktuelle Ansicht. WLAN-Zugangsdaten und der Cover-Schlüssel bleiben ausschließlich in der lokalen, ignorierten Datei `include/secrets.h`.

## Cover-Anzeige

Der Dienst verarbeitet ausschließlich erfolgreiche Import-Webhooks (`eventType=Download`) von Radarr und Sonarr. Poster werden lokal aus den MediaCover-Verzeichnissen gelesen, auf 170 × 320 Pixel zugeschnitten und als RGB565LE an das Display übertragen. Der letzte Zustand wird persistent gespeichert; bei temporären Fehlern wird die Übertragung wiederholt.

Serien-Webhooks übernehmen `episodes[]`; angezeigt wird die höchste Staffel-/Folgennummer des Imports im Format `SxxExx`.

## Kompilieren und Flashen

```sh
pio run
pio run -t upload --upload-port <aktueller-port>
```

Den Port mit `pio device list` ermitteln. Vor dem Kompilieren eine lokale `firmware/include/secrets.h` aus der Vorlage der eigenen Installation anlegen. Diese Datei wird nicht versioniert.

## Speicherpool-Hinweis

Diese Installation verwendet einen gebündelten Speicherpool ohne klassisches Unraid-Array mit Paritätslaufwerk. Die Speicheranzeige basiert deshalb auf dem tatsächlich eingebundenen Pool-Datensatz und nicht auf den üblichen Array-/Parity-Pfaden. Befehle und Mount-Pfade aus diesem Projekt sind installationsabhängig und müssen bei einem Standard-Array oder einer anderen Pool-Konfiguration angepasst und geprüft werden.

## Unraid-Dienst

Der Dienst liegt unter `service/` und wird auf Unraid typischerweise nach `/mnt/user/appdata/unraid-cover/` übertragen.

```sh
cd service
python -m unittest discover -v
docker compose up -d --build
```

Die Webhook-Endpunkte lauten:

- Radarr: `http://<unraid-ip>:8089/webhook/radarr/<webhook-token>`
- Sonarr: `http://<unraid-ip>:8089/webhook/sonarr/<webhook-token>`

In Radarr und Sonarr jeweils **On Import** und **On Upgrade** aktivieren. Zugangsdaten und Tokens gehören ausschließlich in die lokale `.env`.
