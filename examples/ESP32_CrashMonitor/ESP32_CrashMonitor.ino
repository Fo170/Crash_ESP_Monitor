// ============================================================================
// Crash_ESP_Monitor — exemple ESP32
// ============================================================================
// Journalise des événements, détecte un crash au boot, affiche statut + journal.
// Brancher un moniteur série à 115200.
// ============================================================================
#include <Arduino.h>
#include <Crash_ESP_Monitor.h>

#define DEBUG_VERBOSE

void setup()
{
  Serial.begin(115200);
  delay(300);

  INIT_Temp_fct();              // t_fct : uptime
  CM_Init();                    // prépare le journal RTC + déverse un crash précédent

  chip_information();           // CPU/Flash
  boot_info();                  // raison du reset

  CM_Event("BOOT Crash_ESP_Monitor v0.3.1");

  // Si un crash a eu lieu au boot précédent, on l'affiche sur la sortie
  String crash = CM_Resume();
  if (crash.length() > 0)
  {
    Serial.println("\n🚨 CRASH DÉTECTÉ :");
    Serial.println(crash);
  }

  // Métadonnées de diagnostic (compteurs + boucle de reboot)
  Serial.print("Crashs totaux: ");
  Serial.println(cm_rtc.crash_count);
  Serial.println(CM_CrashLoop() ? "🔄 BOUCLE DE REBOOT détectée !" : "Pas de boucle de reboot.");
}

void loop()
{
  Calcule_Temp_fct();           // uptime
  CM_HWM_Core0();
  CM_HWM_Core1();

  static uint32_t dernier = 0;
  if (millis() - dernier >= 10000)
  {
    dernier = millis();
    CM_Event("tache télémétrie (uptime %.0f s)", t_fct);

    if (!CM_HeapOK(20000))      // garde de sécurité heap
      Serial.println("⚠️ heap bas, on évite un appel réseau");
    else
      Serial.println("✅ heap OK, appel réseau possible");

    Serial.println(CM_Statut());
    Serial.println(CM_Journal(5));
  }

  delay(10);
}
