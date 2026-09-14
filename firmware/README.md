# UnraidDisplay mit Cover-Ansicht

## Stand
Aus dem lokalen VS-Code-Dateiverlauf wiederhergestellt. Originalordner war nicht mehr vorhanden. Sicherung: `../UnraidDisplay-recovered-backup.tar.gz`. Der wiederhergestellte Stand ist nicht als identisch mit der aktuell geflashten Firmware verifiziert.

## Analyse
- PlatformIO, Arduino, LilyGO T-Display-S3, ST7789, 170 × 320, Rotation 0.
- TFT_eSPI wurde im alten Bibliotheksverzeichnis auf Setup206 umgestellt. Diese Einstellung wird jetzt im Projekt festgehalten. Die zusätzlich gefundene SPI-Konfiguration in `Setup/Setup/User_Setup.h` war nicht die aktive Setup206-Konfiguration.
- Keine bisherige Tastenlogik. Neue Umschalttaste: GPIO14, entprellt, ein Wechsel pro Druck. BOOT bleibt frei.
- Statusdaten: HTTP GET `http://192.168.178.56:8001/status`, JSON mit CPU, CPU-Temperatur, Speicher, Array, Docker, HDD-Temperaturen und Uptime. Auf Unraid läuft dafür `unraid-display-api`.
- Dashboard-Zeichenfunktionen übernommen; lediglich Division durch null bei ungültiger RAM-Gesamtgröße abgesichert. Netzwerkabfrage separat, damit die Oberfläche bedienbar bleibt. Intervall: fünf Sekunden nach Abschluss einer Abfrage.

## Cover und Speicherung
Der Dienst verwendet nur erfolgreiche Importmeldungen (`eventType=Download`). Tests, Grab, Rename und andere Ereignisse ändern das Cover nicht. Film-/Serien-ID und Titel bleiben in SQLite erhalten; Poster werden aus den lokal vorhandenen MediaCover-Ordnern gelesen, die nur lesend in den Container eingebunden sind. Es sind keine Radarr-/Sonarr- oder TMDB-Schlüssel im Cover-Dienst nötig. Das Poster wird mittig auf 170 × 320 zugeschnitten, als RGB565LE übertragen und mit SHA-256 verifiziert.

Ein Webhook wird erst nach Speicherung in SQLite bestätigt. Bei Ausfällen wird alle zehn Sekunden erneut versucht. Es zählt die Reihenfolge des Eingangs der Webhooks; für verspätet zugestellte Ereignisse lässt sich die tatsächliche frühere Importreihenfolge nicht zuverlässig rekonstruieren. Der neueste Auftrag ersetzt ältere noch ausstehende Aufträge. Ein bereits laufender Upload kann noch fertig werden, bevor der nächste übertragen wird.

Das Gerät speichert zwei Bilddateien abwechselnd. Erst nach vollständiger Prüfung wird der aktive Dateiverweis atomar in NVS umgeschaltet. Ein Stromausfall beim Upload lässt das bisherige Bild bestehen. Der Modus wird ebenfalls in NVS gespeichert. Es gibt keinen Timer zum Löschen oder Zurückschalten. Uploads funktionieren auch im Unraid-Modus. Ein fehlendes Poster lässt das bisherige Bild stehen und wird erneut versucht.

## Erstmalige Inbetriebnahme
1. Projektordner in VS Code öffnen. Eine lokale, nicht versionierte `include/secrets.h` aus der Vorlage deiner Installation anlegen; WLAN-Daten und Cover-Schlüssel werden nicht veröffentlicht.
2. Nach Freigabe des USB-Anschlusses zunächst die bestehende Geräte-Firmware sichern, dann PlatformIO Upload ausführen. **Kein Erase Flash und kein Upload Filesystem** ausführen.
3. Falls die Cover-Ansicht „Speicherfehler“ zeigt: Beim Neustart GPIO14 fünf Sekunden gedrückt halten. Das initialisiert den reservierten LittleFS-Bereich nur dann, wenn noch kein Cover-Commit gespeichert ist. Ein bestehender Cover-Speicher wird nicht automatisch formatiert.
4. Kurzer Druck auf GPIO14 wechselt die Ansicht. IP-Adresse des Geräts für den Dienst ermitteln.

## Unraid-Dienst
`service/` nach `/mnt/user/appdata/unraid-cover/` übertragen. In `.env` ist DISPLAY_URL auf `http://192.168.178.96` gesetzt. Die voreingestellten Arr-Adressen wurden in Unraids Docker-Ansicht bestätigt. COVER_TOKEN muss mit der Firmware übereinstimmen. WEBHOOK_TOKEN ist bereits zufällig erzeugt.

Mit Docker Compose: im Serviceordner `docker compose up -d --build` ausführen. Alternativ:

```sh
docker build -t unraid-cover:local .
docker run -d --name unraid-cover --restart unless-stopped --env-file .env -p 192.168.178.56:8089:8080 -v /mnt/user/appdata/unraid-cover/data:/data -v /mnt/cache/appdata/radarr/MediaCover:/posters/radarr:ro -v /mnt/cache/appdata/sonarr/MediaCover:/posters/sonarr:ro unraid-cover:local
```

Radarr und Sonarr: Einstellungen → Connect → Webhook. Jeweils „On Import“ und falls separat angeboten „On Upgrade“ aktivieren. URL:

- Radarr: `http://192.168.178.56:8089/webhook/radarr/WEBHOOK_TOKEN`
- Sonarr: `http://192.168.178.56:8089/webhook/sonarr/WEBHOOK_TOKEN`

Den Platzhalter durch den Wert aus `.env` ersetzen. Test senden und speichern. Der Test verändert das Cover nicht. Dienst und Gerät sind für das vertrauenswürdige Heimnetz gedacht; keine Portfreigaben ins Internet.

## Lokale Prüfung und offene Schritte
Firmware erfolgreich kompiliert (Arduino 2.0.14, Espressif32 6.5.0, TFT_eSPI 2.5.43, ArduinoJson 7.4.2). Elf Python-Tests prüfen Eventfilter, ungültige IDs, Pixelgröße/Byte-Reihenfolge, fehlerhafte Bilder, SQLite-Neustart, Übertragungswiederholung, überholte Aufträge, unveränderte Bilder, Speicherfehler und den tatsächlichen lokalen HTTP-Eingang inklusive Authentifizierung.

Hardwaretests stehen aus: USB-Zugriff auf `/dev/cu.usbmodem3101` wurde mit `Operation not permitted` abgelehnt; es wurde nicht geflasht. Insbesondere Farben, Taste, Speicherung nach realem Stromausfall und die vollständige Verbindung mit den echten Arr-Instanzen müssen am Gerät geprüft werden. Softwaretests für die Übertragung verwenden simulierte Antworten des Displays.

## Quellen
- Hardware/Setup206/GPIO14: https://github.com/Xinyuan-LilyGO/T-Display-S3
- Radarr-Importereignis: https://github.com/Radarr/Radarr/blob/develop/src/NzbDrone.Core/Notifications/Webhook/WebhookBase.cs
- Sonarr-Importereignis: https://github.com/Sonarr/Sonarr/blob/develop/src/NzbDrone.Core/Notifications/Webhook/WebhookBase.cs

## Installation auf Unraid am 14.09.2026
Container `unraid-cover` installiert unter `/mnt/user/appdata/unraid-cover`, Port 8089, Neustartregel `unless-stopped`. Radarr-Webhook „LilyGO Cover“ ID 12, Sonarr ID 13, jeweils Imports und Upgrades aktiv. Beide Verbindungstests HTTP 200; Erstellung HTTP 201 und anschließend per API bestätigt. Bestehende Benachrichtigungen unverändert. Die Display-IP 192.168.178.96 wurde anhand der Status-Abfragen und der mit der USB-Gerätekennung übereinstimmenden MAC-Adresse ermittelt. Noch keine neue Firmware aufgespielt.

Abschlussprüfung auf Unraid: Je ein echtes Radarr- und Sonarr-Poster erfolgreich in 108800 Byte RGB565 konvertiert. Container läuft mit `unless-stopped`; Healthcheck erfolgreich. Bisherige Status-API weiterhin HTTP 200. Der temporäre Transferdienst auf dem Mac wurde beendet.
