# Crash_ESP_Monitor

Monitoring de **crash** pour **ESP8266** et **ESP32** : journal circulaire **RTC persistant** (survit au reboot), raison de boot, **compteurs de crash par type** + **détection de boucle de reboot**, **WatchDog**, high-water marks pile/heap, garde de sécurité heap — le tout **fusionné en un seul header** avec les utilitaires dont le firmware a besoin (uptime, logs, chip, mémoire).

> **Une seule librairie, un seul fichier `Crash_ESP_Monitor.h`**, sans `.cpp` ni dépendance : fusion « améliorée » de cinq librairies **Fo170** (`t_fct`, `Debug_Fo170`, `boot_info`, `chip_information`, `MemoryInfo`) + **WatchDog** et **monitoring de crash**.
>
> → Détails de l'origine, de la fusion et des avantages dans la section [Origine](#origine--une-fusion-améliorée-de-librairies-fo170).

## Origine : une fusion « améliorée » de librairies Fo170

`Crash_ESP_Monitor.h` regroupe en **un seul fichier header-only** cinq librairies de l'org **Fo170** (chacune disponible séparément sur GitHub) plus un **monitoring de crash** qui n'existe dans aucune d'elles :

| Bloc | Librairie d'origine | Ce qui est fusionné |
|---|---|---|
| 1 | [`t_fct`](https://github.com/Fo170/t_fct) | Uptime (`t_fct`, `INIT_Temp_fct()`, `Calcule_Temp_fct()`) + heure de boot NTP ajoutée (`getBootTimeEpoch()` / `getBootTimeString()`) |
| 2 | [`Debug`](https://github.com/Fo170/Debug) (`Debug_Fo170.h`) | Macros `LOG_INFO/ERROR/WARNING/DEBUG` (fichier:ligne), `BOOT_HALT`, commutateur `DEBUG_VERBOSE` |
| 3 | [`boot_info`](https://github.com/Fo170/boot_info) | Raison du reset : `CM_ResetReason()` / `CM_ResetReasonTexte()` (ESP8266 + ESP32) |
| 4 | [`chip_information`](https://github.com/Fo170/chip_information) | Caractéristiques CPU/Flash (`chip_information()`) |
| 5 | [`MemoryInfo`](https://github.com/Fo170/MemoryInfo) | Heap libre/total/fragmentation + `getLargestFreeBlock()` (bloc contigu, v0.3.3) |
| 6 | *(nouveau)* | **Monitoring de crash** : journal RTC persistant, détection au boot, compteurs par type, boucle de reboot, WatchDog `esp_task_wdt_*`, high-water marks pile/heap, gardes `CM_HeapOK*` |

### La fusion « améliorée »

Chaque bloc a été **réécrit sous une API homogène** plutôt que recopié tel quel : symboles préfixés `CM_*`, portée C++/Arduino unique, comportements communs factorisés et corrections appliquées aux deux plateformes dans le même fichier (magic RTC versionné « MON3 » pour invalider proprement les anciennes données, garde anti-faux-positif v0.3.2, heap réel au crash v0.3.3…). L'ancien découpage en plusieurs `.h` (souvent hors-versionnage, avec dépendances croisées et symboles en conflit) est remplacé par **une seule source de vérité** : une version à bumper, une API à documenter, un fichier à copier.

### Avantages à utiliser celle-ci (plutôt que les libs séparées)

- **Un seul include, zéro `.cpp`** : `#include <Crash_ESP_Monitor.h>` suffit ; utilisable tel quel dans un sketch ou via `lib_deps` PlatformIO.
- **Une seule version synchronisée** (header, `library.json`, `library.properties`, README changelog) au lieu de 5+ dépôts à aligner.
- **Aucune redondance ni collision** : chaque bloc n'est inclus qu'une fois ; pas de symboles dupliqués entre libs.
- **API unifiée ESP8266/ESP32** : la même fonction fonctionne sur les deux cibles (ex. `getLargestFreeBlock()` = `ESP.getMaxFreeBlockSize()` sur ESP8266, `heap_caps_get_largest_free_block` sur ESP32) ; le WatchDog est no-op sur ESP8266.
- **Diagnostic de crash complet** : persistance RTC → le crash est dumpé au boot suivant avec raison, registres, compteurs, boucle de reboot et **heap réel capturé au moment de la panique**.
- **Robustesse mémoire accrue** : gardes `CM_HeapOK` / `CM_HeapOKBloc` (anti-fragmentation) évitent les `abort()` HTTP/TLS sur heap fragmenté.
- **Flash/RAM & maintenance** : un seul fichier préprocessé une fois ; chaîne de maintenance réduite à un dépôt.

## Caractéristiques

- **Header-only** : `src/Crash_ESP_Monitor.h`, tout en `inline`, **aucune dépendance** (ni autre lib, ni `.cpp`). ⚠️ **À inclure dans UN SEUL fichier source du projet** (un seul `.ino`/`.cpp`) : les variables globales partagées (`t_fct`, `cm_rtc`…) doivent avoir une définition unique.
- **Journal RTC persistant** : ESP32 → mémoire `RTC_NOINIT` (40 événements) ; ESP8266 → mémoire RTC utilisateur `system_rtc_mem_*` (512 o → 8 événements).
- **Détection de crash au boot** : raison anormale (panic, WatchDog, brownout…), déverse le journal + registres RTC de diagnostic (`addr2line`) + **compteurs par type** + **détection de boucle de reboot**. ⚠️ **v0.3.2 — garde anti-faux-positif** : si le journal RTC est vide (`head == 0`, premier boot après flashage/RTC vierge), la raison anormale (ex. WatchDog n°7 laissé par esptool) n'est **pas** un vrai crash → `CM_Init()` n'enregistre ni ne dump rien. ⚠️ **v0.3.3** : **heap RÉEL au crash** (hook `esp_register_shutdown_handler` → `crash_heap_reel`/`crash_maxalloc_reel` capturés au moment du restart, affichés dans « Heap au crash ») + garde **`CM_HeapOKBloc(seuil, seuilBloc)`** sensible à la fragmentation (exige un bloc contigu, pas seulement le heap total — évite les `abort()` HTTP/TLS sur heap fragmenté).
- **Hook de sortie** : `Serial` par défaut, ou n'importe quel canal via `CM_SetOutput(callback)` (ex. **SerialWeb**).
- **Garde de sécurité** `CM_HeapOK(seuil)` : à appeler avant un appel réseau/Domoticz/TLS pour éviter un `abort()` par épuisement du heap.

## Installation (PlatformIO)

```ini
lib_deps =
    https://github.com/Fo170/Crash_ESP_Monitor.git@^0.3.4
```

## Utilisation rapide

```cpp
#include <Arduino.h>
#include <Crash_ESP_Monitor.h>

void setup() {
    Serial.begin(115200);
    INIT_Temp_fct();      // t_fct : temps de fonctionnement
    CM_WatchDogInit(3);   // WatchDog 3 s (ESP32) — reboot auto si gel
    CM_WatchDogAdd();
    CM_Init();            // prépare le journal RTC + déverse un crash précédent
    CM_Event("BOOT v1.0");
}

void loop() {
    CM_WatchDogReset();   // alimente le WatchDog
    Calcule_Temp_fct();   // met à jour t_fct
    CM_HWM_Core0();       // high-water mark pile/heap
    CM_HWM_Core1();

    // Garde anti-fragmentation (v0.3.3+) avant un appel réseau/Domoticz :
    if (!CM_HeapOKBloc(30000)) { delay(1000); return; }   // 30 Ko libres ET bloc contigu ≥ 15 Ko
    // ... lecture Domoticz / envoi Telegram ...
}
```

### Récupérer le crash sur le web (hook → SerialWeb)

```cpp
#include <ESPAsyncWebServer.h>
#include <SerialWeb.h>
AsyncWebServer asyncServer(81);
SerialWeb      sw(&asyncServer);

void setup() {
    CM_SetOutput([](const char* l){ sw.println(l); });   // les logs CM_* vont sur le WS
}
```

## API

| Méthode | Description |
|---|---|
| `CM_Init()` | Init RTC + compteur de boots + dump si crash précédent (dans `setup()`) |
| `CM_Event(fmt, ...)` | Journalise un événement (uptime + epoch si NTP) |
| `CM_Resume()` | String du crash du boot précédent (vide sinon) |
| `CM_CrashRecent()` | `true` si le boot précédent a paniqué |
| `CM_Statut()` | String : boots, raison boot, HWM pile/heap, mémoire |
| `CM_Journal(nb)` | String des `nb` derniers événements |
| `CM_HWM_Core0()` / `CM_HWM_Core1()` | High-water marks pile (à appeler dans chaque boucle de tâche) + heap min (v0.3.4) |
| `CM_HeapMaj()` | Heap libre minimum depuis le boot (appelé automatiquement par `CM_HWM_*`) |
| `CM_HeapOK(seuil)` | Garde : `false` si heap libre < seuil (log warning) |
| `CM_HeapOKBloc(seuil, seuilBloc)` | Garde anti-fragmentation : `false` si heap libre < seuil OU plus grand bloc contigu < seuilBloc (défaut seuil/2) — v0.3.3 |
| `CM_SetOutput(cb)` | Hook de sortie `void(*)(const char*)` (défaut Serial) |
| `CM_WatchDogInit(s, panic)` | WatchDog task (ESP32 ; no-op ESP8266) |
| `CM_WatchDogAdd()` | Abonne la tâche courante au WatchDog |
| `CM_WatchDogReset()` | Réalimente le WatchDog (dans chaque boucle) |
| `CM_ResetReason()` / `CM_ResetReasonTexte()` | Numéro + texte de la raison du reset |
| `CM_ReasonAnormale(reason)` | `true` si la raison est un crash (panic, WDT, brownout…) |
| `CM_CrashEnregistrer(reason)` | Incrémente les compteurs + mémorise le dernier crash |
| `CM_CrashLoop()` | Détecte une boucle de reboot (≥ 3 crashs / 5 derniers boots) |
| `CM_CrashResume()` | Résumé du dernier crash (date/heure, uptime, heap, boucle) |
| `CM_TraceJson(JsonDocument&)` | Remplit un doc ArduinoJson (boot, crash, dump, statut, journal, métadonnées) — ArduinoJson optionnel |
| `t_fct` / `Calcule_Temp_fct()` | Uptime en secondes (fusion de la lib t_fct) |
| `LOG_INFO/ERROR/WARNING/DEBUG(...)` | Macros de log (fusion de Debug, fichier:ligne) |
| `boot_info()` | Raison de la dernière réinitialisation |
| `chip_information()` | Caractéristiques CPU/Flash |
| `getFreeMemory()` / `getHeapFragmentation()` / ... | Heap (fusion de MemoryInfo) |

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/Crash_ESP_Monitor.h` | La librairie complète (unique) |
| `examples/ESP32_CrashMonitor/` | Exemple ESP32 |
| `examples/ESP8266_CrashMonitor/` | Exemple ESP8266 |

## Changelog

### v0.3.4
- **« Heap libre min » enfin suivi sur ESP32** : `CM_HWM_Core0()/Core1()` appellent désormais `CM_HeapMaj()` (avant, la valeur restait à 0 si la fonction n'était pas appelée manuellement).
- **Anti-recyclage du heap « réel »** : `crash_heap_reel`/`crash_maxalloc_reel` sont purgés après consommation (détection de crash) et au boot normal — une vieille valeur (reset dur, hook non exécuté) ne ressort plus pour un crash ultérieur. Le bloc contigu du dernier crash est conservé en RAM (`cm_crash_maxalloc_last`).
- **Raison `SDIO` (n°10)** ajoutée à `CM_ResetReasonTexte()`.
- **Horodatage fiable** : seuil NTP passé de `100000` s (faux positif après ~28 h sans synchro) à `CM_EPOCH_FIABLE` (1 000 000 000 s) ; le journal affiche l'**heure locale** (`localtime`) comme le résumé de crash (cohérence fuseau).
- **Garde de taille RTC ESP8266** : `static_assert(sizeof(CMRtcBlock) <= 512)` (mémoire RTC utilisateur) — un futur champ déborderait sinon silencieusement.
- Affichage HWM sans tailles de pile codées en dur « (sur 10000/20000) ».
- Magic RTC inchangée (« MON3 » — aucune modification de structure RTC).

### v0.3.3
- **Heap RÉEL au crash** via hook `esp_register_shutdown_handler` (`crash_heap_reel`/`crash_maxalloc_reel`).
- **`CM_HeapOKBloc(seuil, seuilBloc)`** : garde anti-fragmentation (bloc contigu + heap total).
- **`getLargestFreeBlock()`** : plus grand bloc contigu (ESP32/ESP8266).
- Magic RTC « MON3 » (invalidation des données v0.3.2).

### v0.3.2
- **Garde anti-faux-positif** : journal RTC vide au premier boot → raison anormale ignorée.

### v0.3.1
- Fusion initiale (t_fct + Debug + boot_info + chip + MemoryInfo + WatchDog + monitoring crash) + anneau `crash_boots` (boucle de reboot), magic « MON2 ».

## Licence

GPL-3.0-only — voir `LICENSE`.
