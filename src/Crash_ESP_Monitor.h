// ============================================================================
// Crash_ESP_Monitor.h — v0.3.3
// Librairie header-only (ESP8266 + ESP32) : monitoring de crash + utilitaires
// fusionnés (uptime, debug, boot_info, chip, mémoire, WatchDog) en UN SEUL
// fichier.
//
// Auteur : Olivier FOURNET (org Fo170) — Licence GPL-3.0-only
// https://github.com/Fo170/Crash_ESP_Monitor
//
// ---------------------------------------------------------------
// CE QUI EST FUSIONNÉ (source unique, plus de redondance) :
//   • t_fct            : temps de fonctionnement (t_fct, uptime, boot time)
//   • Debug            : macros LOG_* + BOOT_HALT (DEBUG_VERBOSE)
//   • boot_info        : raison du reset (esp_reset_reason / getResetInfoPtr)
//   • chip_information : caractéristiques CPU/Flash
//   • MemoryInfo       : heap (libre, total, fragmentation, utilisée)
//   • WatchDog         : INIT_/Add/Reset (esp_task_wdt_* ESP32, no-op ESP8266)
//   • monitoring crash : journal circulaire RTC persistant + HWM pile/heap +
//                        garde de sécurité heap + hook de sortie (SerialWeb…)
//
// ---------------------------------------------------------------
// PRINCIPES
//   • Header-only : tout en inline, AUCUN .cpp. Garde d'include unique.
//   • Persistance du journal : ESP32 → mémoire RTC_NOINIT (40 évts) ;
//     ESP8266 → mémoire RTC utilisateur system_rtc_mem_* (512 o → 8 évts).
//   • Sortie : par défaut Serial ; brancher un autre canal via CM_SetOutput()
//     (ex. SerialWeb) — hook void(*)(const char*).
//   • Utilisation (setup) :  Serial.begin(); CM_Init();
//                            INIT_Temp_fct();
//     (boucles) :           Calcule_Temp_fct(); CM_HWM_Core0(); CM_HWM_Core1();
//     (avant un appel réseau/Domoticz) :  if (!CM_HeapOK(seuil)) return;
//
// ---------------------------------------------------------------
// ⚠️ DEBUG_VERBOSE : défini par le firmware AVANT ce include pour activer
//    les logs LOG_DEBUG (comportement identique à Debug_Fo170.h).
// ⚠️ Ce SDK ESP32 (IDF 4.4) n'expose pas esp_set_panic_handler / 
//    esp_set_debug_buffer → pas de backtrace complète automatique. Le journal
//    RTC + les registres RTC de diagnostic permettent en général de localiser
//    la cause (adresse PC → addr2line).
// ============================================================================
#ifndef CRASH_ESP_MONITOR_H
#define CRASH_ESP_MONITOR_H

#include <Arduino.h>

// ============================================================================
// 1) t_fct — Temps de fonctionnement en secondes
// ============================================================================
float t_fct;
unsigned long t0_fct;

void INIT_Temp_fct(void)
{
  t0_fct = millis();
}

void Calcule_Temp_fct(void)   // Temp de fonctionnement en secondes
{
  t_fct = 0.001f * (float)(millis() - t0_fct);
}

// --- NTP : configuration France (utilisé par configTzTime / getBootTime) ---
const char* ntpServer         = "fr.pool.ntp.org";
const long  gmtOffset_sec     = 3600;       // UTC+1 (CET / heure d'hiver)
const int   daylightOffset_sec = 3600;      // +1h pour l'heure d'été (CEST)

time_t getBootTimeEpoch()
{
  time_t now_ntp = time(nullptr);
  if (now_ntp < 1000000000) return 0;               // NTP pas encore sync
  return now_ntp - (time_t)(t_fct + 0.5f);          // arrondi à la seconde
}

String getBootTimeString()
{
  time_t bootEpoch = getBootTimeEpoch();
  if (bootEpoch == 0) return String("NTP non synchronisé");

  struct tm *btm = localtime(&bootEpoch);
  char buf[30];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
           btm->tm_mday, btm->tm_mon + 1, btm->tm_year + 1900,
           btm->tm_hour, btm->tm_min, btm->tm_sec);
  return String(buf);
}

// ============================================================================
// 2) Debug — Macros de log (fichier:ligne injectés), commutateur DEBUG_VERBOSE
// ============================================================================
#ifdef DEBUG_VERBOSE
  #define Serial_PRINTLN_VERBOSE(x)    Serial.println(x)
  #define PRINTF_VERBOSE(x)            printf(x)
#else
  #define Serial_PRINTLN_VERBOSE(x)
  #define PRINTF_VERBOSE(x)
#endif

#define LOG_INFO(msg)          LOG_INFO_AT(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg)         LOG_ERROR_AT(msg, __FILE__, __LINE__)
#define LOG_WARNING(msg)       LOG_WARNING_AT(msg, __FILE__, __LINE__)

#ifdef DEBUG_VERBOSE
  #define LOG_DEBUG(msg)       LOG_DEBUG_AT(msg, __FILE__, __LINE__)
#else
  #define LOG_DEBUG(msg)
#endif

#define LOG_INFO_AT(msg, file, line)    Serial.printf("[INFO] 📄 %s (%s:%d)\n", msg, file, line)
#define LOG_ERROR_AT(msg, file, line)   Serial.printf("[ERROR] ❌ %s (%s:%d)\n", msg, file, line)
#define LOG_WARNING_AT(msg, file, line) Serial.printf("[WARN] ⚠️ %s (%s:%d)\n", msg, file, line)
#define LOG_DEBUG_AT(msg, file, line)   Serial.printf("[DEBUG] 🐛 %s (%s:%d)\n", msg, file, line)

#define LOG_INFO_VALUE(name, val)  Serial.printf("[INFO] 📄 %s = %s (%s:%d)\n", name, String(val).c_str(), __FILE__, __LINE__)
#define LOG_ERROR_VALUE(name, val) Serial.printf("[ERROR] ❌ %s = %s (%s:%d)\n", name, String(val).c_str(), __FILE__, __LINE__)

#define BOOT_HALT(msg) do { \
  Serial.println("-----------boot_halt-------------"); \
  Serial.print("At: "); Serial.print(__FILE__); Serial.print(":"); Serial.println(__LINE__); \
  if(msg){ Serial.print("Reason: "); Serial.println(msg); } \
  Serial.flush(); \
  while(1) {} \
} while(0)

// ============================================================================
// 3) boot_info — Raison de la dernière réinitialisation (ESP8266 + ESP32)
// ============================================================================
#if defined(ESP8266)
  extern "C" {
  #include <user_interface.h>
  }
#endif

// Raison de reset : numéro + texte lisible.
static uint8_t CM_ResetReason(void)
{
#if defined(ESP8266)
  struct rst_info* rinfo = ESP.getResetInfoPtr();
  return (rinfo ? rinfo->reason : 0);
#else
  return (uint8_t)esp_reset_reason();
#endif
}

static String CM_ResetReasonTexte(uint8_t reason)
{
  switch (reason)
  {
    case 1:  return "Power on";
    case 2:  return "Reset externe (pin)";
    case 3:  return "Reset logiciel (esp_restart)";
    case 4:  return "Exception / panique (crash)";
    case 5:  return "Interrupt WatchDog";
    case 6:  return "Task WatchDog (gel d'une tâche)";
    case 7:  return "Autre WatchDog";
    case 8:  return "Deep sleep";
    case 9:  return "Brownout (alimentation)";
    default: return "Inconnue";
  }
}

// Diagnostic complet de la cause de boot / réinitialisation (port série).
static void boot_info(void)
{
  Serial.println("-----------boot_info-------------");
  uint8_t reason = CM_ResetReason();
  Serial.printf("Reset reason: %u — %s\n", reason, CM_ResetReasonTexte(reason).c_str());
#if defined(ESP8266)
  struct rst_info* rinfo = ESP.getResetInfoPtr();
  if (rinfo)
  {
    Serial.printf("rinfo->exccause: %d\n", rinfo->exccause);
    Serial.printf("rinfo->epc1:     0x%08X\n", rinfo->epc1);   // PC fautif
    Serial.printf("rinfo->excvaddr: 0x%08X\n", rinfo->excvaddr);
  }
#endif
  Serial.println("---------------------------------");
}

// ============================================================================
// 3bis) WatchDog — esp_task_wdt (ESP32) / no-op (ESP8266)
// ============================================================================
// Le WatchDog du task WatchDog (ESP32) reboote la carte si une tâche se fige
// (3 s par défaut). La raison du reset renvoyée ensuite distingue WatchDog
// task/interrupt (6/5) d'un panique (4) — pertinent pour le debug des plantages.
// Sur ESP8266 le WDT est géré par le SDK → no-op (pas d'API exposée).
// ============================================================================
#if defined(ESP32)
  #include <esp_task_wdt.h>
#endif

// Initialise le WatchDog du task courant : timeout (s) + comportement panique.
static void CM_WatchDogInit(uint32_t timeout_s, bool panic = true)
{
#if defined(ESP32)
  esp_task_wdt_init(timeout_s, panic);   // panic=true → reboot auto si gel
#endif
}

// Abonne la tâche courante au WatchDog (à appeler dans chaque tâche à surveiller).
static void CM_WatchDogAdd(void)
{
#if defined(ESP32)
  esp_task_wdt_add(NULL);                // current thread
#endif
}

// Réalimente le WatchDog (à appeler régulièrement dans la boucle de chaque tâche).
static void CM_WatchDogReset(void)
{
#if defined(ESP32)
  esp_task_wdt_reset();
#endif
}

// ============================================================================
// 4) chip_information — Caractéristiques CPU/Flash
// ============================================================================
static void chip_information(void)
{
#if defined(ESP8266)
  Serial.printf("\n\nVersion Sdk : %s\n", ESP.getSdkVersion());
  Serial.printf("Version de base : %s\n", ESP.getCoreVersion().c_str());
  Serial.printf("Fréquence du processeur : %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Free heap : %u\n", ESP.getFreeHeap());
#elif defined(ESP32)
  Serial.printf("\n\nChip : %s / %d cœur(s) / rev v%d\n",
                ESP.getChipModel(), ESP.getChipCores(), ESP.getChipRevision());
  Serial.printf("CPU : %u MHz / SDK : %s\n", ESP.getCpuFreqMHz(), ESP.getSdkVersion());
  Serial.printf("Flash : %u Ko\n", ESP.getFlashChipSize() / 1024);
#endif
}

// ============================================================================
// 5) MemoryInfo — Mémoire heap (libre, total, fragmentation, utilisée)
// ============================================================================
struct MemoryInfo {
    int freeMemory;
    int totalMemory;
    int fragmentation;
    bool isAvailable;
};

struct MemoryStats {
    int usedMemory;
    int freeMemory;
    int heapSize;
    int fragmentation;
};

#if defined(ESP8266)
  #include <Esp.h>

  static inline int getFreeMemory()          { return ESP.getFreeHeap(); }
  static inline int getTotalHeap()           { static int total = ESP.getFreeHeap(); return total; }
  static inline int getHeapFragmentation()   { return ESP.getHeapFragmentation(); }
  static inline int getUsedMemory()          { int u = getTotalHeap() - getFreeMemory(); return u < 0 ? 0 : u; }
  static inline int getLargestFreeBlock()    { return ESP.getMaxFreeBlockSize(); }

#elif defined(ESP32)
  #include <esp_heap_caps.h>

  static inline int getFreeMemory()          { return (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT); }
  static inline int getTotalHeap()           { return (int)heap_caps_get_total_size(MALLOC_CAP_DEFAULT); }
  static inline int getHeapFragmentation()   {
    size_t free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    return free == 0 ? 100 : (100 - (largest * 100 / free));
  }
  static inline int getUsedMemory()          {
    return (int)(heap_caps_get_total_size(MALLOC_CAP_DEFAULT) -
                 heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
  }
  static inline int getLargestFreeBlock()    { return (int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT); }

#else
  static inline int getFreeMemory()          { return -1; }
  static inline int getTotalHeap()           { return -1; }
  static inline int getHeapFragmentation()   { return -1; }
  static inline int getUsedMemory()          { return -1; }
  static inline int getLargestFreeBlock()    { return -1; }
#endif

static inline MemoryInfo getMemoryInfo() {
    MemoryInfo info;
    info.freeMemory = getFreeMemory();
    info.totalMemory = getTotalHeap();
    info.fragmentation = getHeapFragmentation();
    info.isAvailable = (info.freeMemory >= 0 && info.totalMemory > 0);
    return info;
}

static inline MemoryStats getMemoryStats() {
    MemoryStats stats;
    stats.freeMemory = getFreeMemory();
    stats.heapSize = getTotalHeap();
    stats.fragmentation = getHeapFragmentation();
    stats.usedMemory = getUsedMemory();
    return stats;
}

static inline float getMemoryUsagePercent() {
    int total = getTotalHeap();
    int used = getUsedMemory();
    return (total <= 0 || used < 0) ? -1.0f : (used * 100.0f) / total;
}

static inline void printMemoryInfo() {
    MemoryInfo info = getMemoryInfo();
    Serial.println(F("=== INFORMATION MÉMOIRE ==="));
    if (!info.isAvailable) { Serial.println(F("  Information mémoire non disponible")); return; }
    Serial.print(F("RAM Total: ")); Serial.print(info.totalMemory); Serial.println(F(" bytes"));
    Serial.print(F("RAM utilisée: ")); Serial.print(getUsedMemory()); Serial.println(F(" bytes"));
    Serial.print(F("RAM libre: ")); Serial.print(info.freeMemory); Serial.println(F(" bytes"));
    if (info.fragmentation >= 0) { Serial.print(F("  Fragmentation: ")); Serial.print(info.fragmentation); Serial.println(F("%")); }
    float usage = getMemoryUsagePercent();
    if (usage >= 0) { Serial.print(F("  Mémoire utilisée: ")); Serial.print(usage, 2); Serial.println(F("%")); }
}

static inline void printMemoryStats() {
    MemoryStats stats = getMemoryStats();
    Serial.println(F("=== STATISTIQUES MÉMOIRE ==="));
    Serial.print(F("  Taille du tas: ")); Serial.print(stats.heapSize); Serial.println(F(" bytes"));
    Serial.print(F("  Mémoire utilisée: ")); Serial.print(stats.usedMemory); Serial.println(F(" bytes"));
    Serial.print(F("  RAM libre: ")); Serial.print(stats.freeMemory); Serial.println(F(" bytes"));
    if (stats.fragmentation >= 0) { Serial.print(F("  Fragmentation: ")); Serial.print(stats.fragmentation); Serial.println(F("%")); }
}

// ============================================================================
// 6) MONITORING DE CRASH — journal RTC persistant + HWM pile/heap + garde heap
// ============================================================================

// ----------------------------------------------------------------------------
// Mémoire RTC persistante (survit au reboot) :
//   ESP32  : RTC_NOINIT_ATTR (mémoire RTC rapide, non réinitialisée au reset)
//   ESP8266: mémoire RTC utilisateur system_rtc_mem_read/write (512 o max)
// ----------------------------------------------------------------------------
#if defined(ESP32)
  #include <esp_attr.h>
  #include <soc/soc.h>
  #include <soc/rtc_cntl_reg.h>
  #include <esp_system.h>       // v0.3.3 : esp_register_shutdown_handler (heap réel au crash)
  #define CM_MAX_EVENTS  40
#elif defined(ESP8266)
  #define CM_MAX_EVENTS  8      // 8 × 48 o = 384 o + entête + métadonnées → 512 o max RTC user
#endif

// V0.3.1 : structure CMRtcBlock agrandie (anneau crash_boots) → magic incrémenté
// pour invalider proprement les données RTC de la v0.3.0 (sinon lecture décalée).
// V0.3.3 : + crash_heap_reel / crash_maxalloc_reel (heap RÉEL au moment du crash,
// capturé par le hook de shutdown) → magic "MON3" (données v0.3.2 invalides).
#define CM_MAGIC       0x4D4F4E33UL   // "MON3"
#define CM_MSG_LEN     40

// Détection d'une boucle de reboot : N crashs sur les M derniers boots.
#define CM_CRASH_LOOP_SEUIL  3   // nb de crashs dans la fenêtre → boucle
#define CM_CRASH_LOOP_BOOTS  5   // fenêtre (derniers boots)

struct CMEvent {
  uint32_t ms;         // millis() au moment de l'événement (boot courant)
  uint32_t epoch;      // time() si NTP synchro (0 sinon)
  char msg[CM_MSG_LEN];
};

// Bloc RTC unique (magie + entête + métadonnées crash + journal) — sur ESP32
// placé en RTC_NOINIT, sur ESP8266 gardé en RAM et rapatrié dans la RTC user.
struct CMRtcBlock {
  uint32_t magic;
  uint32_t head;
  uint32_t boot;
  // --- Métadonnées de crash (diagnostic) ---
  uint32_t crash_count;           // total de crashs (raisons anormales)
  uint32_t crash_par_type[10];    // [reason] → nb (raisons 1..9)
  uint32_t crash_last_epoch;      // epoch du dernier événement avant le crash (0 si NTP pas prêt)
  uint32_t crash_last_uptime_s;   // uptime (ms/1000) du dernier événement avant le crash
  uint32_t crash_last_heap;       // heap au crash (préféré : capturé par le hook de shutdown) — sinon au boot de détection
  uint32_t crash_last_boot;       // n° de boot du dernier crash DÉTECTÉ (au CM_Init qui l'a vu)
  uint32_t crash_heap_reel;       // getFreeMemory() AU CRASH (hook de shutdown, v0.3.3)
  uint32_t crash_maxalloc_reel;   // plus grand bloc contigu AU CRASH (v0.3.3)
  uint32_t crash_boots[CM_CRASH_LOOP_BOOTS];   // anneau des boots des derniers crashs (fenêtre)
  uint32_t crash_boots_head;      // index d'écriture de l'anneau
  CMEvent ring[CM_MAX_EVENTS];
};

#if defined(ESP32)
  RTC_NOINIT_ATTR CMRtcBlock cm_rtc;
  static CMRtcBlock& cm_rtc_ref = cm_rtc;
  #define CM_RTC_LOAD()  do { } while (0)
  #define CM_RTC_SAVE()  do { } while (0)
#else
  static CMRtcBlock cm_rtc;
  #define CM_RTC_LOAD()  system_rtc_mem_read(0,  &cm_rtc, sizeof(cm_rtc))
  #define CM_RTC_SAVE()  system_rtc_mem_write(0, &cm_rtc, sizeof(cm_rtc))
#endif

// ----------------------------------------------------------------------------
// Hook de sortie : canal de log (Serial par défaut). Brancher SerialWeb via
// CM_SetOutput([](const char* l){ sw.println(l); });
// ----------------------------------------------------------------------------
typedef void (*CM_OutputFn)(const char*);
static CM_OutputFn cm_output = nullptr;

static void CM_SetOutput(CM_OutputFn fn) { cm_output = fn; }

static void CM_emit(const char* line)
{
  if (cm_output) cm_output(line);
  else           Serial.println(line);
}

// ----------------------------------------------------------------------------
// État de monitoring (RAM, remis à zéro à chaque boot).
// ----------------------------------------------------------------------------
static bool    cm_crash_recent = false;   // dernier boot = panique ?
static String  cm_crash_dump   = "";      // journal du crash (RAM)

static void CM_Event(const char* fmt, ...);   // fwd (définie plus bas)
static String CM_Journal(int nb);             // fwd (définie plus bas)

#if defined(ESP32)
  static volatile uint32_t cm_stack_hwm_core0 = 0;
  static volatile uint32_t cm_stack_hwm_core1 = 0;
#endif
static volatile uint32_t cm_heap_low = 0xFFFFFFFFUL;

// ----------------------------------------------------------------------------
// Journalise un événement dans la RTC (circulaire). Utilisable partout :
//   CM_Event("BOOT v%s", version);
//   CM_Event("MCP domoticz_historique idx=%u", idx);
// ----------------------------------------------------------------------------
static void CM_Event(const char* fmt, ...)
{
  if (cm_rtc.magic != CM_MAGIC)
  {
    memset(&cm_rtc, 0, sizeof(cm_rtc));     // RTC vierge ou effacée
    cm_rtc.magic = CM_MAGIC;
    cm_rtc.head  = 0;
    cm_rtc.boot  = 0;
    CM_RTC_SAVE();
  }

  char buf[CM_MSG_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  CMEvent& ev = cm_rtc.ring[cm_rtc.head % CM_MAX_EVENTS];
  ev.ms    = millis();
  ev.epoch = (uint32_t)time(nullptr);
  strncpy(ev.msg, buf, CM_MSG_LEN - 1);
  ev.msg[CM_MSG_LEN - 1] = '\0';
  cm_rtc.head++;
  CM_RTC_SAVE();
}

// ----------------------------------------------------------------------------
// High-water marks : valeurs mini de pile/heap libres depuis le boot.
//   ESP32 : pile par tâche (uxTaskGetStackHighWaterMark).
//   ESP8266 : pas d'API de pile par tâche → on trace le heap min.
// À appeler dans chaque boucle (CM_HWM_Core0 dans la tâche Core0, etc.).
// ----------------------------------------------------------------------------
static void CM_HeapMaj(void)
{
  uint32_t h = (uint32_t)getFreeMemory();
  if (h < cm_heap_low) cm_heap_low = h;
}

static void CM_HWM_Core0(void)
{
#if defined(ESP32)
  cm_stack_hwm_core0 = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
#else
  CM_HeapMaj();
#endif
}

static void CM_HWM_Core1(void)
{
#if defined(ESP32)
  cm_stack_hwm_core1 = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
#else
  CM_HeapMaj();
#endif
}

// ----------------------------------------------------------------------------
// Garde de sécurité heap : retourne false si le heap libre < seuil (o), et
// journalise. À appeler AVANT tout appel réseau/Domoticz/TLS pour éviter un
// crash par épuisement du heap (malloc échoué → abort()).
// ----------------------------------------------------------------------------
static bool CM_HeapOK(uint32_t seuil)
{
  uint32_t libre = (uint32_t)getFreeMemory();
  if (libre < seuil)
  {
    CM_Event("⚠️ Heap bas: %u o < %u o", libre, seuil);
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// v0.3.3 : garde de sécurité heap "fragmentation-aware". En plus du heap libre
// TOTAL, on exige un plus grand bloc CONTIGU suffisant (getMaxAllocHeap) : un
// heap fragmenté (ex. 58 %) peut laisser 30 Ko libres mais aucun bloc contigu
// de ~8-10 Ko → malloc HTTP/TLS échoue → abort() → panic (raison n°4). Retourne
// false si total < seuil OU plus grand bloc < seuilBloc (défaut seuil/2).
// ----------------------------------------------------------------------------
static bool CM_HeapOKBloc(uint32_t seuil, uint32_t seuilBloc = 0)
{
  if (seuilBloc == 0) seuilBloc = seuil / 2;
  uint32_t libre  = (uint32_t)getFreeMemory();
  uint32_t blocMax = (uint32_t)getLargestFreeBlock();
  if (libre < seuil || blocMax < seuilBloc)
  {
    CM_Event("⚠️ Heap bas: %u o libre, bloc max %u o (seuil %u/bloc %u)",
             libre, blocMax, seuil, seuilBloc);
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// v0.3.3 : hook de shutdown (esp_register_shutdown_handler). Exécuté par
// esp_restart() — y compris APRÈS un panic (la panique se termine par un
// esp_restart) — donc juste AVANT le reboot : on capture le heap RÉEL au moment
// du crash dans la RTC. Au boot suivant, CM_CrashEnregistrer l'expose comme
// "Heap au crash" (au lieu du heap trompeur du boot de détection).
// ----------------------------------------------------------------------------
static void CM_ShutdownHook(void)
{
  if (cm_rtc.magic != CM_MAGIC) return;
  cm_rtc.crash_heap_reel     = (uint32_t)getFreeMemory();
  cm_rtc.crash_maxalloc_reel = (uint32_t)getLargestFreeBlock();
  CM_RTC_SAVE();
}

// ----------------------------------------------------------------------------
// Init (setup(), APRÈS Serial.begin) : prépare la RTC, incrémente le compteur
// de boots et, si la raison du reset est un crash, déverse le journal + les
// registres RTC de diagnostic sur la sortie (hook/série).
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Raison "anormale" (crash) : PANIC, WatchDog (task/interrupt/autre), brownout,
// SDIO... Exclut 1 (power on), 2 (ext), 3 (software), 8 (deep sleep) qui sont
// des redémarrages volontaires/normaux.
// ----------------------------------------------------------------------------
static bool CM_ReasonAnormale(uint8_t reason)
{
  return (reason >= 4 && reason != 8);
}

// ----------------------------------------------------------------------------
// Enregistre un crash : incrémente le compteur total + par type, et mémorise le
// dernier crash.
// ⚠️ Appelé depuis CM_Init() au DÉBUT du boot, AVANT INIT_Temp_fct() : t_fct vaut
// encore 0 et l'heure NTP du boot courant n'a pas de sens pour un crash survenu
// au boot PRÉCÉDENT. On dérive donc epoch/uptime du DERNIER ÉVÉNEMENT du journal
// RTC (il date du boot précédent, c'est le plus proche du crash).
// ----------------------------------------------------------------------------
static void CM_CrashEnregistrer(uint8_t reason)
{
  cm_rtc.crash_count++;
  if (reason > 0 && reason <= 9) cm_rtc.crash_par_type[reason]++;
  cm_rtc.crash_last_boot = cm_rtc.boot;

  // v0.3.3 : on préfère le heap RÉEL capturé au moment du crash par le hook de
  // shutdown (crash_heap_reel), sinon on retombe sur le heap du boot de détection
  // (trompeur : juste après un warm reboot, le heap est quasi au max).
  if (cm_rtc.crash_heap_reel > 0)
    cm_rtc.crash_last_heap = cm_rtc.crash_heap_reel;
  else
    cm_rtc.crash_last_heap = (uint32_t)getFreeMemory();

  // Anneau des boots des crashs (fenêtre glissante pour CM_CrashLoop).
  cm_rtc.crash_boots[cm_rtc.crash_boots_head % CM_CRASH_LOOP_BOOTS] = cm_rtc.boot;
  cm_rtc.crash_boots_head++;

  // Dernier événement du journal (boot précédent, juste avant le crash).
  if (cm_rtc.head > 0)
  {
    const CMEvent& dern = cm_rtc.ring[(cm_rtc.head - 1) % CM_MAX_EVENTS];
    cm_rtc.crash_last_epoch    = dern.epoch;          // heure NTP du dernier évènement
    cm_rtc.crash_last_uptime_s = dern.ms / 1000;      // uptime à cet événement
  }
  else
  {
    cm_rtc.crash_last_epoch    = 0;
    cm_rtc.crash_last_uptime_s = 0;
  }
  CM_RTC_SAVE();
}

// ----------------------------------------------------------------------------
// Détection d'une boucle de reboot : au moins CM_CRASH_LOOP_SEUIL crashs DANS la
// fenêtre des CM_CRASH_LOOP_BOOTS derniers boots (anneau crash_boots, comparé au
// boot courant — le boot de détection d'un crash = celui qui l'a vu au CM_Init).
// ----------------------------------------------------------------------------
static bool CM_CrashLoop(void)
{
  if (cm_rtc.crash_count < CM_CRASH_LOOP_SEUIL) return false;
  uint32_t nb = 0;
  uint32_t fenetre = (cm_rtc.boot > CM_CRASH_LOOP_BOOTS) ? cm_rtc.boot - CM_CRASH_LOOP_BOOTS : 0;
  for (uint32_t i = 0; i < CM_CRASH_LOOP_BOOTS; i++)
  {
    uint32_t b = cm_rtc.crash_boots[i];
    if (b && b > fenetre && b <= cm_rtc.boot) nb++;
  }
  return (nb >= CM_CRASH_LOOP_SEUIL);
}

// ----------------------------------------------------------------------------
// Résumé détaillé du dernier crash : date/heure (si NTP) + uptime + heap.
// Pour /etat, Telegram, web.
// ----------------------------------------------------------------------------
static String CM_CrashResume(void)
{
  String s = "Dernier crash: boot n°" + String(cm_rtc.crash_last_boot);
  if (cm_rtc.crash_last_epoch >= 100000)
  {
    time_t e = (time_t)cm_rtc.crash_last_epoch;
    struct tm* t = localtime(&e);
    char buf[30];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min, t->tm_sec);
    s += " le " + String(buf);
  }
  s += "\nUptime au crash: " + String(cm_rtc.crash_last_uptime_s) + " s";
  s += "\nHeap au crash: " + String(cm_rtc.crash_last_heap) + " o";
  if (cm_rtc.crash_maxalloc_reel > 0)
    s += " (bloc contigu max " + String(cm_rtc.crash_maxalloc_reel) + " o)";
  if (CM_CrashLoop()) s += "\n🔄 BOUCLE DE REBOOT détectée !";
  return s;
}

// ----------------------------------------------------------------------------
// Init (setup(), APRÈS Serial.begin) : prépare la RTC, incrémente le compteur
// de boots, enregistre les métadonnées si le reset est anormal, et déverse le
// journal + registres de diagnostic sur la sortie (hook/série).
// ----------------------------------------------------------------------------
static void CM_Init(void)
{
  CM_RTC_LOAD();
  if (cm_rtc.magic != CM_MAGIC)
  {
    memset(&cm_rtc, 0, sizeof(cm_rtc));
    cm_rtc.magic = CM_MAGIC;
    cm_rtc.head  = 0;
    cm_rtc.boot  = 0;
    CM_RTC_SAVE();
  }
  cm_rtc.boot++;
  CM_RTC_SAVE();

#if defined(ESP32)
  // v0.3.3 : capture du heap RÉEL au moment d'un crash (esp_restart est appelé
  // par la panique → le hook s'exécute juste avant le reboot). Enregistré UNE
  // seule fois (les hooks de shutdown ne se doublent pas — sinon err).
  static bool hookEnregistre = false;
  if (!hookEnregistre)
  {
    hookEnregistre = true;
    esp_register_shutdown_handler(CM_ShutdownHook);
  }
#endif

  uint8_t reason = CM_ResetReason();
  if (CM_ReasonAnormale(reason))   // panic, WatchDog, brownout...
  {
    // ⚠️ V0.3.2 : garde anti-faux-positif — si le journal RTC est VIDE (head == 0),
    // aucune session précédente n'a tourné (premier boot après flashage, RTC vierge) :
    // la raison anormale (ex. WatchDog n°7 laissé par esptool) est un reliquat de
    // l'init, PAS un vrai crash → on n'enregistre ni ne dump rien.
    if (cm_rtc.head == 0)
    {
      Serial.println("CM_Init: reset anormal (n°" + String(reason) + " — " +
                     CM_ResetReasonTexte(reason) + ") mais journal RTC vide → faux positif ignoré");
      return;
    }

    CM_CrashEnregistrer(reason);   // métadonnées (compteurs + dernier crash)
    cm_crash_recent = true;

#if defined(ESP32)
    uint32_t diag0 = REG_READ(RTC_CNTL_DIAG0_REG);
    uint32_t diag1 = REG_READ(RTC_CNTL_DIAG1_REG);
#else
    uint32_t diag0 = 0, diag1 = 0;
    struct rst_info* rinfo = ESP.getResetInfoPtr();
    if (rinfo) { diag0 = rinfo->epc1; diag1 = rinfo->excvaddr; }
#endif

    cm_crash_dump = "🚨 CRASH précédent détecté (boot n°" + String(cm_rtc.boot) + ")\n";
    cm_crash_dump += "Raison: n°" + String(reason) + " — " + CM_ResetReasonTexte(reason) + "\n";
    cm_crash_dump += CM_CrashResume() + "\n";
    cm_crash_dump += "Crashs totaux: " + String(cm_rtc.crash_count) + "\n";
    cm_crash_dump += "Registres RTC diag (bruts): 0x" + String(diag0, HEX) +
                     " 0x" + String(diag1, HEX) + "\n";
    cm_crash_dump += "(ESP32: traduire avec xtensa-esp32-elf-addr2line -pfiaC firmware.elf <adr>)";
#if defined(ESP8266)
    cm_crash_dump += "\n(ESP8266: epc1 = PC fautif → addr2line sur l'adresse)";
#endif
    cm_crash_dump += "\n--- Journal avant le crash (le + récent en bas) ---\n";
    cm_crash_dump += CM_Journal(CM_MAX_EVENTS);

    Serial.println("\n========== CRASH MONITOR ==========");
    Serial.println(cm_crash_dump);
    Serial.println("====================================");
    CM_emit(cm_crash_dump.c_str());
  }
}

// ----------------------------------------------------------------------------
// Résumé du crash du boot précédent (vide si pas de crash) — pour le web et
// Telegram. Le drapeau reste ACTIF pendant toute la session de boot.
// ----------------------------------------------------------------------------
static String CM_Resume(void)
{
  return cm_crash_recent ? cm_crash_dump : "";
}

static bool CM_CrashRecent(void)  { return cm_crash_recent; }

// ----------------------------------------------------------------------------
// Statut : boots, HWM pile/heap, mémoire. Pour /etat, web, débogage.
// ----------------------------------------------------------------------------
static String CM_Statut(void)
{
  String s = "Traçage:\n";
  s += "Boots: " + String(cm_rtc.boot) + "\n";
  s += "Raison boot: n°" + String(CM_ResetReason()) + " — " +
       CM_ResetReasonTexte(CM_ResetReason()) + "\n";
#if defined(ESP32)
  s += "Stack HWM Core0: " + String(cm_stack_hwm_core0) + " o libres (sur 10000)\n";
  s += "Stack HWM Core1: " + String(cm_stack_hwm_core1) + " o libres (sur 20000)\n";
#endif
  s += "Heap libre min: " + String(cm_heap_low == 0xFFFFFFFFUL ? 0 : (uint32_t)cm_heap_low) + " o\n";
  s += "Heap libre: " + String(getFreeMemory()) + " o (fragmentation " +
       String(getHeapFragmentation()) + " %)\n";
  s += "Crashs: " + String(cm_rtc.crash_count) + " au total";
  if (cm_rtc.crash_count > 0)
  {
    s += " (panic " + String(cm_rtc.crash_par_type[4]);
    s += ", WDT " + String(cm_rtc.crash_par_type[5] + cm_rtc.crash_par_type[6] + cm_rtc.crash_par_type[7]);
    s += ", brownout " + String(cm_rtc.crash_par_type[9]) + ")";
    s += "\n" + CM_CrashResume();
  }
  s += "\n";
  return s;
}

// ----------------------------------------------------------------------------
// Journal : les `nb` derniers événements (ou tous si nb <= 0), du + ancien au
// + récent. Horodatage : heure NTP si dispo, sinon +XXXs depuis le boot.
// ----------------------------------------------------------------------------
static String CM_Journal(int nb)
{
  if (cm_rtc.magic != CM_MAGIC || cm_rtc.head == 0) return "Journal vide";
  if (nb <= 0 || nb > CM_MAX_EVENTS) nb = CM_MAX_EVENTS;
  String s;
  uint32_t debut = cm_rtc.head > (uint32_t)nb ? cm_rtc.head - (uint32_t)nb : 0;
  for (uint32_t i = debut; i < cm_rtc.head; i++)
  {
    const CMEvent& ev = cm_rtc.ring[i % CM_MAX_EVENTS];
    char ts[24];
    if (ev.epoch >= 100000)
    {
      struct tm* t = gmtime((const time_t*)&ev.epoch);
      snprintf(ts, sizeof(ts), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    }
    else snprintf(ts, sizeof(ts), "+%lus", (unsigned long)(ev.ms / 1000));
    s += String(ts) + "  " + String(ev.msg) + "\n";
  }
  return s;
}

// ----------------------------------------------------------------------------
// Remplit un JsonDocument ArduinoJson (si présent) avec l'état de traçage :
//   boot, crash, dump, statut, journal(40).
// ⚠️ NÉCESSAITE <ArduinoJson.h> inclus AVANT (dépendance OPTIONNELLE — la lib
// reste utilisable sans). Idéal pour une route web /api/trace sans dépendre du
// serveur HTTP ici :
//   JsonDocument doc; CM_TraceJson(doc); serializeJson(doc, json); server.send(...);
// ----------------------------------------------------------------------------
#if defined(ARDUINOJSON_VERSION_MAJOR)
static bool CM_TraceJson(JsonDocument& doc)
{
  doc["boot"]    = cm_rtc.boot;
  doc["crash"]   = CM_CrashRecent();
  doc["dump"]    = CM_Resume();
  doc["statut"]  = CM_Statut();
  doc["journal"] = CM_Journal(40);
  // Métadonnées de diagnostic des crashs
  doc["crash_count"]     = cm_rtc.crash_count;
  JsonArray parType = doc["crash_par_type"].to<JsonArray>();
  for (int r = 1; r <= 9; r++) parType.add(cm_rtc.crash_par_type[r]);
  doc["crash_last_epoch"]    = cm_rtc.crash_last_epoch;
  doc["crash_last_uptime_s"] = cm_rtc.crash_last_uptime_s;
  doc["crash_last_heap"]     = cm_rtc.crash_last_heap;
  doc["crash_last_boot"]     = cm_rtc.crash_last_boot;
  doc["crash_loop"]          = CM_CrashLoop();
  return true;
}
#endif

#endif // CRASH_ESP_MONITOR_H
