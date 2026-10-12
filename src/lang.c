/* psp5 – made by Mille Weber */
/* Alla texter i gränssnittet. En rad per text, en kolumn per språk i ordningen
 * engelska, svenska, tyska, franska, spanska, italienska, nederländska,
 * portugisiska, ryska, japanska. Saknas en översättning används engelska. */
#include <psputility.h>
#include <string.h>
#include "lang.h"
#include "system.h"

#define NL (LANG_COUNT - 1) /* antal riktiga språk */

unsigned char g_lang_setting = LANG_AUTO;
static int s_lang = LANG_EN;

static const char *const T[S_COUNT][NL] = {
    [S_TAB_GAMES] = { "Games", "Spel", "Spiele", "Jeux", "Juegos", "Giochi", "Games", "Jogos", "Игры", "ゲーム" },
    [S_TAB_APPS] = { "Apps", "Appar", "Apps", "Applis", "Apps", "App", "Apps", "Apps", "Приложения", "アプリ" },
    [S_LAUNCH] = { "Start", "Starta", "Starten", "Lancer", "Iniciar", "Avvia", "Starten", "Iniciar", "Запуск", "スタート" },
    [S_HINT_TAB] = { "Tab", "Flik", "Tab", "Onglet", "Pestaña", "Scheda", "Tab", "Aba", "Вкладка", "タブ" },
    [S_SETTINGS] = { "Settings", "Inställningar", "Einstellungen", "Paramètres", "Ajustes", "Impostazioni",
                     "Instellingen", "Definições", "Настройки", "設定" },
    [S_NO_GAMES] = { "No games found", "Inga spel hittades", "Keine Spiele gefunden", "Aucun jeu trouvé",
                     "No se encontraron juegos", "Nessun gioco trovato", "Geen games gevonden",
                     "Nenhum jogo encontrado", "Игры не найдены", "ゲームが見つかりません" },
    [S_NO_APPS] = { "No apps found", "Inga appar hittades", "Keine Apps gefunden", "Aucune appli trouvée",
                    "No se encontraron apps", "Nessuna app trovata", "Geen apps gevonden", "Nenhuma app encontrada",
                    "Приложения не найдены", "アプリが見つかりません" },
    [S_HINT_GAMES] = { "Put ISO, CSO or ZSO files in " PSP5_ROOT "/ISO", "Lägg ISO, CSO eller ZSO i " PSP5_ROOT "/ISO",
                       "Lege ISO-, CSO- oder ZSO-Dateien in " PSP5_ROOT "/ISO", "Placez vos fichiers ISO, CSO ou ZSO dans " PSP5_ROOT "/ISO",
                       "Copia archivos ISO, CSO o ZSO en " PSP5_ROOT "/ISO", "Copia file ISO, CSO o ZSO in " PSP5_ROOT "/ISO",
                       "Zet ISO-, CSO- of ZSO-bestanden in " PSP5_ROOT "/ISO", "Coloque ficheiros ISO, CSO ou ZSO em " PSP5_ROOT "/ISO",
                       "Поместите файлы ISO, CSO или ZSO в " PSP5_ROOT "/ISO", "ISO・CSO・ZSOを " PSP5_ROOT "/ISO に入れてください" },
    [S_HINT_APPS] = { "Put apps in " PSP5_ROOT "/PSP/GAME", "Lägg appar i " PSP5_ROOT "/PSP/GAME", "Lege Apps in " PSP5_ROOT "/PSP/GAME",
                      "Placez les applis dans " PSP5_ROOT "/PSP/GAME", "Copia las apps en " PSP5_ROOT "/PSP/GAME",
                      "Copia le app in " PSP5_ROOT "/PSP/GAME", "Zet apps in " PSP5_ROOT "/PSP/GAME", "Coloque as apps em " PSP5_ROOT "/PSP/GAME",
                      "Поместите приложения в " PSP5_ROOT "/PSP/GAME", "アプリを " PSP5_ROOT "/PSP/GAME に入れてください" },
    [S_NO_ARK] = { "ARK not found. Games can only be started on a PSP",
                   "ARK hittades inte. Spel kan bara startas på PSP:n",
                   "ARK nicht gefunden. Spiele starten nur auf einer PSP",
                   "ARK introuvable. Les jeux ne démarrent que sur une PSP",
                   "No se encontró ARK. Los juegos solo se inician en una PSP",
                   "ARK non trovato. I giochi si avviano solo su una PSP",
                   "ARK niet gevonden. Games starten alleen op een PSP",
                   "ARK não encontrado. Os jogos só iniciam numa PSP",
                   "ARK не найден. Игры запускаются только на PSP",
                   "ARKが見つかりません。ゲームはPSPでのみ起動できます" },
    [S_LOADING] = { "Loading games", "Läser in spel", "Spiele werden geladen", "Chargement des jeux",
                    "Cargando juegos", "Caricamento giochi", "Games laden", "A carregar jogos", "Загрузка игр",
                    "ゲームを読み込み中" },
    [S_SAVED] = { "Saved. Changes apply after a restart", "Sparat. Ändringarna gäller efter omstart",
                  "Gespeichert. Gilt nach einem Neustart", "Enregistré. Actif après redémarrage",
                  "Guardado. Se aplica al reiniciar", "Salvato. Valido dopo il riavvio",
                  "Opgeslagen. Geldt na een herstart", "Guardado. Aplica-se após reiniciar",
                  "Сохранено. Действует после перезапуска", "保存しました。再起動後に反映されます" },
    [S_LAUNCH_FAILED] = { "Couldn't start the game", "Kunde inte starta spelet", "Spiel konnte nicht gestartet werden",
                          "Impossible de lancer le jeu", "No se pudo iniciar el juego", "Impossibile avviare il gioco",
                          "Kan de game niet starten", "Não foi possível iniciar o jogo", "Не удалось запустить игру",
                          "ゲームを起動できませんでした" },
    [S_USB_MODE] = { "USB mode", "USB-läge", "USB-Modus", "Mode USB", "Modo USB", "Modalità USB", "USB-modus",
                     "Modo USB", "Режим USB", "USBモード" },
    [S_USB_CONNECTED] = { "Connected to computer", "Ansluten till datorn", "Mit dem Computer verbunden",
                          "Connecté à l'ordinateur", "Conectado al ordenador", "Connesso al computer",
                          "Verbonden met computer", "Ligado ao computador", "Подключено к компьютеру", "パソコンに接続中" },
    [S_USB_WAITING] = { "Waiting for USB cable", "Väntar på USB-kabel", "Warte auf USB-Kabel", "En attente du câble USB",
                        "Esperando el cable USB", "In attesa del cavo USB", "Wachten op USB-kabel", "À espera do cabo USB",
                        "Ожидание USB-кабеля", "USBケーブルを待っています" },
    [S_USB_EJECT] = { "Eject the PSP on your computer before leaving", "Mata ut PSP:n i datorn innan du avslutar",
                      "Wirf die PSP am Computer aus, bevor du beendest", "Éjectez la PSP de l'ordinateur avant de quitter",
                      "Expulsa la PSP en el ordenador antes de salir", "Espelli la PSP dal computer prima di uscire",
                      "Werp de PSP uit op je computer voordat je stopt", "Ejete a PSP no computador antes de sair",
                      "Извлеките PSP на компьютере перед выходом", "終了する前にパソコンでPSPを取り出してください" },
    [S_USB_EXIT] = { "Exit USB", "Avsluta USB", "USB beenden", "Quitter USB", "Salir de USB", "Esci da USB",
                     "USB afsluiten", "Sair do USB", "Выйти из USB", "USBを終了" },
    [S_SELECT] = { "Select", "Välj", "Auswählen", "Choisir", "Elegir", "Seleziona", "Kiezen", "Escolher", "Выбрать", "決定" },
    [S_BACK] = { "Back", "Tillbaka", "Zurück", "Retour", "Atrás", "Indietro", "Terug", "Voltar", "Назад", "戻る" },
    [S_YES] = { "Yes", "Ja", "Ja", "Oui", "Sí", "Sì", "Ja", "Sim", "Да", "はい" },
    [S_NO] = { "No", "Nej", "Nein", "Non", "No", "No", "Nee", "Não", "Нет", "いいえ" },
    [S_CONFIRM] = { "Press again to confirm", "Tryck igen för att bekräfta", "Zum Bestätigen erneut drücken",
                    "Appuyez à nouveau pour confirmer", "Pulsa otra vez para confirmar", "Premi di nuovo per confermare",
                    "Druk nogmaals om te bevestigen", "Prima de novo para confirmar", "Нажмите ещё раз",
                    "もう一度押して確定" },
    [S_OFF] = { "Off", "Av", "Aus", "Désactivé", "Desactivado", "Disattivato", "Uit", "Desligado", "Выкл", "オフ" },
    [S_AUTO] = { "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Auto", "Авто", "自動" },
    [S_STANDARD] = { "Default", "Standard", "Standard", "Par défaut", "Predeterminado", "Predefinito", "Standaard",
                     "Padrão", "По умолчанию", "標準" },
    [S_ALL] = { "All", "Alla", "Alle", "Tous", "Todos", "Tutti", "Alle", "Todos", "Все", "すべて" },
    [S_CAT_PERF] = { "Performance", "Prestanda", "Leistung", "Performances", "Rendimiento", "Prestazioni",
                     "Prestaties", "Desempenho", "Скорость", "パフォーマンス" },
    [S_CAT_SYSTEM] = { "System", "System", "System", "Système", "Sistema", "Sistema", "Systeem", "Sistema", "Система",
                       "システム" },
    [S_CAT_PLUGINS] = { "Plugins", "Plugins", "Plugins", "Plugins", "Plugins", "Plugin", "Plug-ins", "Plugins",
                        "Плагины", "プラグイン" },
    [S_CAT_POWER] = { "Power and USB", "Ström och USB", "Strom und USB", "Énergie et USB", "Energía y USB",
                      "Energia e USB", "Stroom en USB", "Energia e USB", "Питание и USB", "電源とUSB" },
    [S_DESC_PSP5] = {
        "ARK settings are saved in SETTINGS.TXT and apply after a restart. Hold SELECT while the PSP starts to skip them and go to the XMB.",
        "ARK-inställningar sparas i SETTINGS.TXT och gäller efter omstart. Håll SELECT när PSP:n startar för att hoppa över dem och komma till XMB.",
        "ARK-Einstellungen werden in SETTINGS.TXT gespeichert und gelten nach einem Neustart. Halte beim Start SELECT gedrückt, um sie zu überspringen.",
        "Les réglages ARK sont enregistrés dans SETTINGS.TXT et s'appliquent après redémarrage. Maintenez SELECT au démarrage pour les ignorer.",
        "Los ajustes de ARK se guardan en SETTINGS.TXT y se aplican al reiniciar. Mantén SELECT al encender la PSP para omitirlos.",
        "Le impostazioni di ARK sono salvate in SETTINGS.TXT e valgono dopo il riavvio. Tieni premuto SELECT all'avvio per saltarle.",
        "ARK-instellingen staan in SETTINGS.TXT en gelden na een herstart. Houd SELECT ingedrukt bij het opstarten om ze over te slaan.",
        "As definições do ARK ficam em SETTINGS.TXT e aplicam-se após reiniciar. Mantenha SELECT ao ligar a PSP para as ignorar.",
        "Настройки ARK хранятся в SETTINGS.TXT и действуют после перезапуска. Удерживайте SELECT при включении, чтобы пропустить их.",
        "ARKの設定はSETTINGS.TXTに保存され、再起動後に反映されます。起動時にSELECTを押し続けるとスキップしてXMBに入ります。" },
    [S_DESC_PERF] = {
        "A higher clock speed makes games run smoother but uses more battery. Above 333 MHz is overclocking.",
        "Högre klockfrekvens ger jämnare spel men drar mer batteri. Över 333 MHz är överklockning.",
        "Ein höherer Takt lässt Spiele flüssiger laufen, braucht aber mehr Akku. Über 333 MHz ist Übertaktung.",
        "Une fréquence plus élevée rend les jeux plus fluides mais use plus la batterie. Au-delà de 333 MHz, c'est de l'overclocking.",
        "Una frecuencia más alta hace los juegos más fluidos, pero gasta más batería. Por encima de 333 MHz es overclock.",
        "Una frequenza più alta rende i giochi più fluidi ma consuma più batteria. Oltre 333 MHz è overclock.",
        "Een hogere kloksnelheid maakt games soepeler maar kost meer batterij. Boven 333 MHz is overklokken.",
        "Uma frequência mais alta torna os jogos mais fluidos, mas gasta mais bateria. Acima de 333 MHz é overclock.",
        "Высокая частота делает игры плавнее, но расходует больше заряда. Выше 333 МГц — разгон.",
        "クロックを上げるとゲームが滑らかになりますが、バッテリーを多く使います。333 MHzを超えるとオーバークロックです。" },
    [S_DESC_PLUGINS] = {
        "Plugins are loaded by ARK on the next start. Hold START while the PSP starts to skip them all.",
        "Plugins laddas av ARK vid nästa start. Håll START när PSP:n startar för att hoppa över alla.",
        "Plugins werden von ARK beim nächsten Start geladen. Halte beim Start START gedrückt, um alle zu überspringen.",
        "Les plugins sont chargés par ARK au prochain démarrage. Maintenez START au démarrage pour tous les ignorer.",
        "ARK carga los plugins en el próximo inicio. Mantén START al encender la PSP para omitirlos todos.",
        "I plugin vengono caricati da ARK al prossimo avvio. Tieni premuto START all'avvio per saltarli tutti.",
        "Plug-ins worden bij de volgende start door ARK geladen. Houd START ingedrukt bij het opstarten om ze over te slaan.",
        "Os plugins são carregados pelo ARK no próximo arranque. Mantenha START ao ligar a PSP para os ignorar.",
        "Плагины загружаются ARK при следующем запуске. Удерживайте START при включении, чтобы пропустить их.",
        "プラグインは次回起動時にARKが読み込みます。起動時にSTARTを押し続けるとすべてスキップします。" },
    [S_BOOT_PSP5] = { "Boot into psp5", "Starta psp5 vid uppstart", "Direkt in psp5 starten", "Démarrer sur psp5",
                      "Iniciar en psp5", "Avvia in psp5", "Opstarten in psp5", "Arrancar no psp5", "Загружаться в psp5",
                      "psp5で起動" },
    [S_SOUNDS] = { "Interface sounds", "Systemljud", "Menütöne", "Sons de l'interface", "Sonidos de la interfaz",
                   "Suoni dell'interfaccia", "Interfacegeluiden", "Sons da interface", "Звуки интерфейса", "操作音" },
    [S_LANGUAGE] = { "Language", "Språk", "Sprache", "Langue", "Idioma", "Lingua", "Taal", "Idioma", "Язык", "言語" },
    [S_IS_LAUNCHER] = { "psp5 is ARK's launcher", "psp5 är ARK:s launcher", "psp5 ist ARKs Launcher",
                        "psp5 est le lanceur d'ARK", "psp5 es el launcher de ARK", "psp5 è il launcher di ARK",
                        "psp5 is de launcher van ARK", "psp5 é o launcher do ARK", "psp5 — лаунчер ARK",
                        "psp5はARKのランチャー" },
    [S_CLEAR_CACHE] = { "Clear image cache", "Rensa bildcache", "Bildcache leeren", "Vider le cache d'images",
                        "Borrar caché de imágenes", "Svuota cache immagini", "Afbeeldingscache wissen",
                        "Limpar cache de imagens", "Очистить кэш картинок", "画像キャッシュを消去" },
    [S_VERSION] = { "Version", "Version", "Version", "Version", "Versión", "Versione", "Versie", "Versão", "Версия",
                    "バージョン" },
    [S_CPU_GAME] = { "CPU in games", "CPU i spel", "CPU in Spielen", "CPU en jeu", "CPU en juegos", "CPU nei giochi",
                     "CPU in games", "CPU nos jogos", "ЦП в играх", "ゲーム中のCPU" },
    [S_CPU_XMB] = { "CPU in XMB and launcher", "CPU i XMB och launcher", "CPU im XMB und Launcher",
                    "CPU dans le XMB et le lanceur", "CPU en XMB y launcher", "CPU in XMB e launcher",
                    "CPU in XMB en launcher", "CPU no XMB e launcher", "ЦП в XMB и лаунчере", "XMBとランチャーのCPU" },
    [S_EXTRA_MEM] = { "Extra memory", "Extra minne", "Zusatzspeicher", "Mémoire supplémentaire", "Memoria extra",
                      "Memoria extra", "Extra geheugen", "Memória extra", "Доп. память", "追加メモリ" },
    [S_MS_CACHE] = { "Memory Stick cache", "Minneskortscache", "Memory-Stick-Cache", "Cache Memory Stick",
                     "Caché de Memory Stick", "Cache Memory Stick", "Memory Stick-cache", "Cache do Memory Stick",
                     "Кэш Memory Stick", "メモリースティックキャッシュ" },
    [S_ISO_CACHE] = { "ISO cache (Inferno)", "ISO-cache (Inferno)", "ISO-Cache (Inferno)", "Cache ISO (Inferno)",
                      "Caché ISO (Inferno)", "Cache ISO (Inferno)", "ISO-cache (Inferno)", "Cache ISO (Inferno)",
                      "Кэш ISO (Inferno)", "ISOキャッシュ (Inferno)" },
    [S_USB_CHARGE] = { "USB charging", "USB-laddning", "USB-Laden", "Charge USB", "Carga por USB", "Ricarica USB",
                       "Opladen via USB", "Carregamento USB", "Зарядка по USB", "USB充電" },
    [S_WPA2] = { "WPA2 for Wi-Fi", "WPA2 för Wi-Fi", "WPA2 für WLAN", "WPA2 pour le Wi-Fi", "WPA2 para Wi-Fi",
                 "WPA2 per Wi-Fi", "WPA2 voor wifi", "WPA2 para Wi-Fi", "WPA2 для Wi-Fi", "Wi-FiでWPA2" },
    [S_SKIP_LOGOS] = { "Skip logos", "Hoppa över logotyper", "Logos überspringen", "Passer les logos",
                       "Omitir logotipos", "Salta i loghi", "Logo's overslaan", "Saltar logótipos", "Пропуск логотипов",
                       "ロゴをスキップ" },
    [S_HIDE_PICS] = { "Hide game pictures in XMB", "Dölj spelbilder i XMB", "Spielbilder im XMB ausblenden",
                      "Masquer les images dans le XMB", "Ocultar imágenes en el XMB", "Nascondi immagini nell'XMB",
                      "Afbeeldingen in XMB verbergen", "Ocultar imagens no XMB", "Скрыть картинки в XMB",
                      "XMBのゲーム画像を隠す" },
    [S_HIDE_MAC] = { "Hide MAC address", "Dölj MAC-adress", "MAC-Adresse verbergen", "Masquer l'adresse MAC",
                     "Ocultar dirección MAC", "Nascondi indirizzo MAC", "MAC-adres verbergen", "Ocultar endereço MAC",
                     "Скрыть MAC-адрес", "MACアドレスを隠す" },
    [S_HIDE_DLC] = { "Hide DLC", "Dölj DLC", "DLC ausblenden", "Masquer les DLC", "Ocultar DLC", "Nascondi DLC",
                     "DLC verbergen", "Ocultar DLC", "Скрыть DLC", "DLCを隠す" },
    [S_NO_LED] = { "Turn off LEDs", "Släck lamporna", "LEDs ausschalten", "Éteindre les LED", "Apagar los LED",
                   "Spegni i LED", "LED's uitzetten", "Desligar os LED", "Выключить индикаторы", "LEDを消灯" },
    [S_NO_UMD] = { "Disable UMD drive", "Stäng av UMD-enheten", "UMD-Laufwerk deaktivieren", "Désactiver le lecteur UMD",
                   "Desactivar lector UMD", "Disattiva lettore UMD", "UMD-station uitschakelen", "Desativar leitor UMD",
                   "Отключить привод UMD", "UMDドライブを無効化" },
    [S_NO_ANALOG] = { "Disable analog stick", "Stäng av analogspaken", "Analogstick deaktivieren",
                      "Désactiver le stick analogique", "Desactivar joystick analógico", "Disattiva levetta analogica",
                      "Analoge stick uitschakelen", "Desativar analógico", "Отключить аналоговый стик",
                      "アナログスティックを無効化" },
    [S_VITA_MUTE] = { "Mute with HOME + volume", "Ljud av med HOME + volym", "Stumm mit HOME + Lautstärke",
                      "Muet avec HOME + volume", "Silenciar con HOME + volumen", "Muto con HOME + volume",
                      "Dempen met HOME + volume", "Silenciar com HOME + volume", "Без звука: HOME + громкость",
                      "HOME+音量でミュート" },
    [S_QA_FLAGS] = { "QA flags", "QA-flaggor", "QA-Flags", "Drapeaux QA", "Indicadores QA", "Flag QA", "QA-vlaggen",
                     "Flags QA", "Флаги QA", "QAフラグ" },
    [S_UMD_REGION] = { "UMD region", "UMD-region", "UMD-Region", "Région UMD", "Región UMD", "Regione UMD", "UMD-regio",
                       "Região UMD", "Регион UMD", "UMDリージョン" },
    [S_XMB_REGION] = { "XMB region", "XMB-region", "XMB-Region", "Région XMB", "Región XMB", "Regione XMB", "XMB-regio",
                       "Região XMB", "Регион XMB", "XMBリージョン" },
    [S_NO_PLUGINS] = { "No plugins installed", "Inga plugins installerade", "Keine Plugins installiert",
                       "Aucun plugin installé", "No hay plugins instalados", "Nessun plugin installato",
                       "Geen plug-ins geïnstalleerd", "Nenhum plugin instalado", "Плагины не установлены",
                       "プラグインはありません" },
    [S_TO_XMB] = { "Exit to XMB", "Gå till XMB", "Zum XMB", "Aller au XMB", "Ir al XMB", "Vai all'XMB", "Naar XMB",
                   "Ir para o XMB", "Выйти в XMB", "XMBへ" },
    [S_SLEEP] = { "Sleep", "Viloläge", "Standby", "Veille", "Suspender", "Sospensione", "Slaapstand", "Suspender",
                  "Сон", "スリープ" },
    [S_RESTART] = { "Restart", "Starta om", "Neu starten", "Redémarrer", "Reiniciar", "Riavvia", "Herstarten",
                    "Reiniciar", "Перезапуск", "再起動" },
    [S_SHUTDOWN] = { "Shut down", "Stäng av", "Ausschalten", "Éteindre", "Apagar", "Spegni", "Uitschakelen",
                     "Desligar", "Выключить", "電源を切る" },
    [S_RECOVERY] = { "Recovery menu", "Återställningsmeny", "Wiederherstellungsmenü", "Menu de récupération",
                     "Menú de recuperación", "Menu di ripristino", "Herstelmenu", "Menu de recuperação",
                     "Меню восстановления", "リカバリーメニュー" },
    [S_LOGOS_GAME] = { "On game start", "Vid spelstart", "Beim Spielstart", "Au lancement d'un jeu",
                       "Al iniciar un juego", "All'avvio di un gioco", "Bij gamestart", "Ao iniciar jogos",
                       "При запуске игры", "ゲーム起動時" },
    [S_LOGOS_BOOT] = { "On boot", "Vid uppstart", "Beim Einschalten", "Au démarrage", "Al encender", "All'accensione",
                       "Bij opstarten", "Ao ligar", "При включении", "起動時" },
    [S_R_JAPAN] = { "Japan", "Japan", "Japan", "Japon", "Japón", "Giappone", "Japan", "Japão", "Япония", "日本" },
    [S_R_AMERICA] = { "America", "Amerika", "Amerika", "Amérique", "América", "America", "Amerika", "América", "Америка",
                      "アメリカ" },
    [S_R_EUROPE] = { "Europe", "Europa", "Europa", "Europe", "Europa", "Europa", "Europa", "Europa", "Европа", "ヨーロッパ" },
    [S_R_KOREA] = { "Korea", "Korea", "Korea", "Corée", "Corea", "Corea", "Korea", "Coreia", "Корея", "韓国" },
    [S_R_UK] = { "United Kingdom", "Storbritannien", "Großbritannien", "Royaume-Uni", "Reino Unido", "Regno Unito",
                 "Verenigd Koninkrijk", "Reino Unido", "Великобритания", "イギリス" },
    [S_R_LATAM] = { "Latin America", "Latinamerika", "Lateinamerika", "Amérique latine", "Latinoamérica",
                    "America Latina", "Latijns-Amerika", "América Latina", "Латинская Америка", "中南米" },
    [S_R_AUSTRALIA] = { "Australia", "Australien", "Australien", "Australie", "Australia", "Australia", "Australië",
                        "Austrália", "Австралия", "オーストラリア" },
    [S_R_HONGKONG] = { "Hong Kong", "Hongkong", "Hongkong", "Hong Kong", "Hong Kong", "Hong Kong", "Hongkong",
                       "Hong Kong", "Гонконг", "香港" },
    [S_R_TAIWAN] = { "Taiwan", "Taiwan", "Taiwan", "Taïwan", "Taiwán", "Taiwan", "Taiwan", "Taiwan", "Тайвань", "台湾" },
    [S_R_RUSSIA] = { "Russia", "Ryssland", "Russland", "Russie", "Rusia", "Russia", "Rusland", "Rússia", "Россия",
                     "ロシア" },
    [S_R_CHINA] = { "China", "Kina", "China", "Chine", "China", "Cina", "China", "China", "Китай", "中国" },
    [S_RL_PSP] = { "PSP games", "PSP-spel", "PSP-Spiele", "Jeux PSP", "Juegos PSP", "Giochi PSP", "PSP-games",
                   "Jogos PSP", "Игры PSP", "PSPゲーム" },
    [S_LANG_AUTO] = { "Automatic", "Automatiskt", "Automatisch", "Automatique", "Automático", "Automatica",
                      "Automatisch", "Automático", "Автоматически", "自動" },
    [S_TAB_LIBRARY] = { "Library", "Bibliotek", "Bibliothek", "Bibliothèque", "Biblioteca", "Libreria",
                        "Bibliotheek", "Biblioteca", "Библиотека", "ライブラリー" },
    [S_LIB_ALL] = { "All games and apps", "Alla spel och appar", "Alle Spiele und Apps", "Tous les jeux et applis",
                    "Todos los juegos y apps", "Tutti i giochi e le app", "Alle games en apps",
                    "Todos os jogos e apps", "Все игры и приложения", "すべてのゲームとアプリ" },
    [S_SORT] = { "Sort", "Sortera", "Sortieren", "Trier", "Ordenar", "Ordina", "Sorteren", "Ordenar",
                 "Сортировка", "並べ替え" },
    [S_SORT_NAME] = { "Name (A-Z)", "Namn (A-Ö)", "Name (A-Z)", "Nom (A-Z)", "Nombre (A-Z)", "Nome (A-Z)",
                      "Naam (A-Z)", "Nome (A-Z)", "Имя (А-Я)", "名前順" },
    [S_SORT_RECENT] = { "Recently added", "Senast tillagda", "Zuletzt hinzugefügt", "Ajoutés récemment",
                        "Añadidos recientemente", "Aggiunti di recente", "Onlangs toegevoegd",
                        "Adicionados recentemente", "Недавно добавленные", "最近追加した順" },
    [S_BOOT_ANIM] = { "Startup animation", "Startanimation", "Startanimation", "Animation de démarrage",
                      "Animación de inicio", "Animazione di avvio", "Opstartanimatie", "Animação de arranque",
                      "Заставка при запуске", "起動アニメーション" },
    /* hälsovarningen på startskärmen – rader skiljs med \n */
    [S_HEALTH] = { "PHOTOSENSITIVITY WARNING: SOME PEOPLE MAY HAVE SEIZURES WHEN EXPOSED TO FLASHING LIGHTS\n"
                   "OR PATTERNS, EVEN WITHOUT ANY HISTORY OF EPILEPSY. IF YOU FEEL UNWELL WHILE PLAYING,\n"
                   "STOP IMMEDIATELY AND CONSULT A DOCTOR. TAKE REGULAR BREAKS.",
                   "VARNING FÖR LJUSKÄNSLIGHET: VISSA PERSONER KAN FÅ ANFALL AV BLINKANDE LJUS ELLER MÖNSTER,\n"
                   "ÄVEN UTAN TIDIGARE EPILEPSI. OM DU MÅR DÅLIGT NÄR DU SPELAR, SLUTA GENAST OCH\n"
                   "KONTAKTA EN LÄKARE. TA REGELBUNDNA PAUSER." },
    [S_TAB_MUSIC] = { "Music", "Musik", "Musik", "Musique", "Música", "Musica", "Muziek", "Música", "Музыка",
                      "ミュージック" },
    [S_NO_MUSIC] = { "No music found", "Ingen musik hittades", "Keine Musik gefunden", "Aucune musique trouvée",
                     "No se encontró música", "Nessuna musica trovata", "Geen muziek gevonden",
                     "Nenhuma música encontrada", "Музыка не найдена", "ミュージックが見つかりません" },
    [S_HINT_MUSIC] = { "Put MP3 files in " PSP5_ROOT "/MUSIC", "Lägg MP3-filer i " PSP5_ROOT "/MUSIC", "Lege MP3-Dateien in " PSP5_ROOT "/MUSIC",
                       "Placez vos fichiers MP3 dans " PSP5_ROOT "/MUSIC", "Copia archivos MP3 en " PSP5_ROOT "/MUSIC",
                       "Copia file MP3 in " PSP5_ROOT "/MUSIC", "Zet MP3-bestanden in " PSP5_ROOT "/MUSIC",
                       "Coloque ficheiros MP3 em " PSP5_ROOT "/MUSIC", "Поместите файлы MP3 в " PSP5_ROOT "/MUSIC",
                       "MP3を " PSP5_ROOT "/MUSIC に入れてください" },
    [S_NOW_PLAYING] = { "Now playing", "Spelas nu", "Wird gespielt", "En cours", "Reproduciendo", "In riproduzione",
                        "Speelt nu", "A tocar", "Сейчас играет", "再生中" },
    [S_NOTHING_PLAYING] = { "Nothing playing", "Inget spelas", "Keine Wiedergabe", "Aucune lecture",
                            "Nada en reproducción", "Nessuna riproduzione", "Er speelt niets", "Nada a tocar",
                            "Ничего не играет", "再生していません" },
    [S_PLAY] = { "Play", "Spela", "Abspielen", "Lire", "Reproducir", "Riproduci", "Afspelen", "Tocar",
                 "Играть", "再生" },
    [S_PAUSE] = { "Pause", "Paus", "Pause", "Pause", "Pausa", "Pausa", "Pauze", "Pausa", "Пауза", "一時停止" },
    [S_TRACK] = { "Track", "Spår", "Titel", "Piste", "Pista", "Brano", "Nummer", "Faixa", "Трек", "曲" },
    [S_LOADING_MUSIC] = { "Loading music", "Läser in musik", "Musik wird geladen", "Chargement de la musique",
                          "Cargando música", "Caricamento musica", "Muziek laden", "A carregar música",
                          "Загрузка музыки", "ミュージックを読み込み中" },
    [S_UNKNOWN_ARTIST] = { "Unknown artist", "Okänd artist", "Unbekannter Künstler", "Artiste inconnu",
                           "Artista desconocido", "Artista sconosciuto", "Onbekende artiest",
                           "Artista desconhecido", "Неизвестный исполнитель", "不明なアーティスト" },
    [S_SAFE_START] = { "Safe start: the last start didn't finish", "Felsäker start: förra starten blev inte klar",
                       "Sicherer Start: letzter Start nicht beendet", "Démarrage sûr : le dernier n'a pas abouti",
                       "Inicio seguro: el último inicio no terminó", "Avvio sicuro: l'ultimo avvio non è finito",
                       "Veilige start: vorige start niet voltooid", "Arranque seguro: o último não terminou",
                       "Безопасный запуск: прошлый не завершился", "セーフモード：前回の起動が完了しませんでした" },
};

static const char *const NAMES[LANG_COUNT] = { "", "English", "Svenska", "Deutsch", "Français", "Español",
                                               "Italiano", "Nederlands", "Português", "Русский", "日本語" };
static const char *const CODES[LANG_COUNT] = { "auto", "en", "sv", "de", "fr", "es", "it", "nl", "pt", "ru", "ja" };

/* PSP:ns systemspråk → vårt språk (koreanska och kinesiska → engelska) */
static int from_system(void)
{
    int sys = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &sys) < 0)
        return LANG_EN;
    switch (sys) {
    case PSP_SYSTEMPARAM_LANGUAGE_JAPANESE:   return LANG_JA;
    case PSP_SYSTEMPARAM_LANGUAGE_FRENCH:     return LANG_FR;
    case PSP_SYSTEMPARAM_LANGUAGE_SPANISH:    return LANG_ES;
    case PSP_SYSTEMPARAM_LANGUAGE_GERMAN:     return LANG_DE;
    case PSP_SYSTEMPARAM_LANGUAGE_ITALIAN:    return LANG_IT;
    case PSP_SYSTEMPARAM_LANGUAGE_DUTCH:      return LANG_NL;
    case PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE: return LANG_PT;
    case PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN:    return LANG_RU;
    default:                                  return LANG_EN;
    }
}

void lang_apply(void)
{
    s_lang = g_lang_setting > LANG_AUTO && g_lang_setting < LANG_COUNT ? g_lang_setting : from_system();
}

const char *L(StrId id)
{
    if (id >= S_COUNT)
        return "";
    const char *s = T[id][s_lang - 1];
    return s ? s : T[id][LANG_EN - 1];
}

const char *lang_name(int id)
{
    if (id == LANG_AUTO)
        return L(S_LANG_AUTO);
    return id > 0 && id < LANG_COUNT ? NAMES[id] : "";
}

const char *lang_code(int id)
{
    return id >= 0 && id < LANG_COUNT ? CODES[id] : "auto";
}

int lang_from_code(const char *code)
{
    for (int i = 0; i < LANG_COUNT; i++)
        if (!strncmp(code, CODES[i], strlen(CODES[i])))
            return i;
    return LANG_AUTO;
}

int lang_needs_cjk(void)
{
    return s_lang == LANG_JA || s_lang == LANG_RU;
}
