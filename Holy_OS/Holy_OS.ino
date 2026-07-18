/*
 * ============================================================================
 * HOLY OS - PENTECOSTAL BIBLE, PRAYER & DISCIPLESHIP ENGINE
 * Hardware: M5Stack Cardputer ADV (ESP32-S3)
 * Dev Website:  https://www.ethicalhackersden.org
 * Dev GitHub:   https://github.com/M5RogueOps
 * 
 * Dependencies (via Arduino Library Manager):
 *   - M5Cardputer (by M5Stack) https://m5stack.com
 *   - ArduinoJson (v7.x by Benoit Blanchon) https://github.com/bblanchon/ArduinoJson
 *   - API Provider: https://bible-api.com
 * ============================================================================
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

// ================= USER CONFIGURATION =================
// Wi-Fi credentials are NOT stored here anymore - see the Settings screen
// (Home -> [7]). This keeps the compiled .bin safe to share with others;
// each device stores its own SSID/password in on-device flash (NVS).
const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET    = 0;     // Adjust for your timezone in seconds
const int   DST_OFFSET    = 3600;  // Daylight savings offset in seconds
// ======================================================

// UI Colors (Parchment Paper Palette - light, easy-to-read theme)
#define COLOR_BG        0xFFDD  // Parchment Page (near-white ivory)
#define COLOR_HEADER    0x6A04  // Deep Leather Brown (header/footer bars & box borders)
#define COLOR_GOLD      0xCCC7  // Antique Gold (accents, labels, highlights)
#define COLOR_TEXT      0xFFBC  // Bright Ivory (light text - ONLY for the dark header/footer bars)
#define COLOR_INK       0x28E1  // Dark Sepia Ink (reading text on parchment/light backgrounds)
#define COLOR_MUTED     0x6AA6  // Muted Umber (secondary/hint text on parchment)
#define COLOR_XP_BAR    0x4BC8  // Sage Green (success/progress)
#define COLOR_ACCENT    0x8963  // Sealing Wax Red (alerts)

// ================= LAYOUT CONSTANTS =================
// Header occupies y:0-18, Footer occupies y:120-135.
// All body text is confined (via clip rect) to this box so it can never
// draw over the header or footer, no matter how far the user scrolls/pages.
const int CONTENT_Y      = 20;
const int CONTENT_BOTTOM = 106;                       // leaves room for the nav-hint bar
const int CONTENT_H      = CONTENT_BOTTOM - CONTENT_Y; // 86 px tall
const int CONTENT_X      = 6;
const int CONTENT_WIDTH  = 228;                       // usable text width in px
const int LINE_HEIGHT    = 11;
const int CHAR_WIDTH     = 6;                         // approx width of size-1 font glyph
const int LINES_PER_PAGE = CONTENT_H / LINE_HEIGHT;   // = 7 lines/page at current sizes

// Application States
enum AppState {
    STATE_HOME,
    STATE_READER,
    STATE_SEARCH,
    STATE_JOURNAL_VIEW,
    STATE_JOURNAL_WRITE,
    STATE_QUESTS,
    STATE_BIBLE_READ,
    STATE_SETTINGS,
    STATE_CREDITS
};
AppState currentState = STATE_HOME;

// Gamification & Storage
Preferences prefs;
int userXP = 0;
int userLevel = 1;
int dailyReadCount = 0;
int dailyGoal = 3;
int lastLoginDay = -1;

// Lifetime stats (never reset daily) - used to drive the Discipleship Missions
int totalReadCount = 0;         // Daily Word passages + Bible chapters ever claimed
int totalPrayerCount = 0;       // Prayers ever logged
int totalBibleChaptersRead = 0; // Bible chapters ever claimed (subset of totalReadCount)

// Pentecostal "Daily Manna" Curated Verses
const char* SPIRIT_VERSES[] = {
    "Acts 1:8", "Acts 2:1-4", "Acts 2:38", "Acts 4:31", "Acts 10:44-46",
    "1 Corinthians 12:4-11", "1 Corinthians 14:1", "Joel 2:28-29",
    "Romans 8:11", "Romans 8:26", "Galatians 5:22-23", "Ephesians 5:18",
    "2 Timothy 1:6", "Zechariah 4:6", "Isaiah 61:1", "Luke 4:18",
    "John 14:16-17", "John 7:37-39", "Jude 1:20", "Mark 16:17-18"
};
const int TOTAL_SPIRIT_VERSES = 20;
int currentMannaIndex = 0;

// Active Data Buffers
String activeReference = "Acts 2:1-4";
String activeScripture = "Press [SPACE] to fetch the Word of God from heaven's servers!";
std::vector<String> activeScriptureLines;
String inputBuffer = "";
String statusMessage = "Blessed be the Lord! Select an option.";
int readerScrollLine = 0; // scroll offset measured in wrapped LINES, not pixels
bool readerXPClaimed = false; // guards against spamming [C] for infinite XP on one passage

// ================= FULL BIBLE INDEX (66 books, KJV chapter counts) =================
struct BibleBook { const char* name; uint8_t chapters; };
const BibleBook BIBLE_BOOKS[] = {
    {"Genesis", 50}, {"Exodus", 40}, {"Leviticus", 27}, {"Numbers", 36}, {"Deuteronomy", 34},
    {"Joshua", 24}, {"Judges", 21}, {"Ruth", 4}, {"1 Samuel", 31}, {"2 Samuel", 24},
    {"1 Kings", 22}, {"2 Kings", 25}, {"1 Chronicles", 29}, {"2 Chronicles", 36}, {"Ezra", 10},
    {"Nehemiah", 13}, {"Esther", 10}, {"Job", 42}, {"Psalms", 150}, {"Proverbs", 31},
    {"Ecclesiastes", 12}, {"Song of Solomon", 8}, {"Isaiah", 66}, {"Jeremiah", 52}, {"Lamentations", 5},
    {"Ezekiel", 48}, {"Daniel", 12}, {"Hosea", 14}, {"Joel", 3}, {"Amos", 9},
    {"Obadiah", 1}, {"Jonah", 4}, {"Micah", 7}, {"Nahum", 3}, {"Habakkuk", 3},
    {"Zephaniah", 3}, {"Haggai", 2}, {"Zechariah", 14}, {"Malachi", 4}, {"Matthew", 28},
    {"Mark", 16}, {"Luke", 24}, {"John", 21}, {"Acts", 28}, {"Romans", 16},
    {"1 Corinthians", 16}, {"2 Corinthians", 13}, {"Galatians", 6}, {"Ephesians", 6}, {"Philippians", 4},
    {"Colossians", 4}, {"1 Thessalonians", 5}, {"2 Thessalonians", 3}, {"1 Timothy", 6}, {"2 Timothy", 4},
    {"Titus", 3}, {"Philemon", 1}, {"Hebrews", 13}, {"James", 5}, {"1 Peter", 5},
    {"2 Peter", 3}, {"1 John", 5}, {"2 John", 1}, {"3 John", 1}, {"Jude", 1},
    {"Revelation", 22}
};
const int TOTAL_BIBLE_BOOKS = sizeof(BIBLE_BOOKS) / sizeof(BIBLE_BOOKS[0]);

// ================= DISCIPLESHIP MISSIONS (sequential unlock chain) =================
enum MissionType { MISSION_READ, MISSION_PRAYER, MISSION_LEVEL, MISSION_BIBLE_CHAPTER };

struct Mission {
    const char* title;
    const char* description;
    MissionType type;
    int threshold;
    int xpReward;
};

// Missions unlock strictly in order - completing one reveals the next.
const Mission MISSIONS[] = {
    { "First Steps",            "Read your first Daily Word passage.",       MISSION_READ,           1,   50  },
    { "Baptized",               "Reach Level 2.",                            MISSION_LEVEL,          2,   150 },
    { "Speak to Heaven",        "Log your first prayer at the Altar.",       MISSION_PRAYER,         1,   50  },
    { "Open the Scroll",        "Read your first chapter of the Bible.",     MISSION_BIBLE_CHAPTER,  1,   75  },
    { "Planted by the Water",   "Read 5 passages or chapters total.",        MISSION_READ,           5,   75  },
    { "Consistent in Prayer",   "Log 5 prayers at the Altar.",               MISSION_PRAYER,         5,   100 },
    { "Rooted & Grounded",      "Read 15 passages or chapters total.",       MISSION_READ,           15,  100 },
    { "Walking in the Spirit",  "Reach Level 5.",                            MISSION_LEVEL,          5,   150 },
    { "Diligent Student",       "Read 10 Bible chapters total.",             MISSION_BIBLE_CHAPTER,  10,  150 },
    { "Deep Roots",             "Read 40 passages or chapters total.",       MISSION_READ,           40,  175 },
    { "Intercessor",            "Log 15 prayers at the Altar.",              MISSION_PRAYER,         15,  200 },
    { "Anointed Vessel",        "Reach Level 10.",                           MISSION_LEVEL,          10,  250 },
    { "Awoken One",             "Reach Level 20.",                           MISSION_LEVEL,          20,  550 },
    { "Blessed One",            "Reach Level 30.",                           MISSION_LEVEL,          30,  1550 },
    { "Book Starter",           "Read 15 Bible chapters.",                   MISSION_BIBLE_CHAPTER,  15,  200 },
    { "Chapter Reader",         "Read 20 Bible chapters.",                   MISSION_BIBLE_CHAPTER,  20,  225 },
    { "Verse Reciter",          "Read 50 Bible chapters.",                   MISSION_BIBLE_CHAPTER,  50,  250 },
    { "Quote Memorizer",        "Read 100 Bible chapters.",                  MISSION_BIBLE_CHAPTER,  100, 275 },
    { "Proverb Collector",      "Read 10 Proverbs.",                         MISSION_BIBLE_CHAPTER,  25,  300 },
    { "Parable Explainer",      "Read 5 Parables.",                          MISSION_BIBLE_CHAPTER,  30,  325 },
    { "Story Teller",           "Read 10 Gospel chapters.",                  MISSION_BIBLE_CHAPTER,  40,  350 },
    { "Poetry Reader",          "Read 10 Psalms.",                           MISSION_BIBLE_CHAPTER,  50,  375 },
    { "Song Writer",            "Log 20 prayers.",                           MISSION_PRAYER,         20,  400 },
    { "Dance Choreographer",    "Log 25 prayers.",                           MISSION_PRAYER,         25,  425 },
    { "Tabret Shaker",          "Read 60 chapters.",                         MISSION_BIBLE_CHAPTER,  60,  450 },
    { "Flute Melodist",         "Read 70 chapters.",                         MISSION_BIBLE_CHAPTER,  70,  475 },
    { "Lute Strummer",          "Reach Level 35.",                           MISSION_LEVEL,          35,  500 },
    { "Drum Beater",            "Log 30 prayers.",                           MISSION_PRAYER,         30,  525 },
    { "Trumpet Blower",         "Read 80 chapters.",                         MISSION_BIBLE_CHAPTER,  80,  550 },
    { "Cymbal Player",          "Read 90 chapters.",                         MISSION_BIBLE_CHAPTER,  90,  575 },
    { "Harp Tuner",             "Log 35 prayers.",                           MISSION_PRAYER,         35,  600 },
    { "Choir Singer",           "Reach Level 40.",                           MISSION_LEVEL,          40,  650 },
    { "Bell Ringer",            "Read 100 chapters.",                        MISSION_BIBLE_CHAPTER,  100, 700 },
    { "Courtyard Guard",        "Read 110 chapters.",                        MISSION_BIBLE_CHAPTER,  110, 750 },
    { "Pavement Polisher",      "Log 40 prayers.",                           MISSION_PRAYER,         40,  800 },
    { "Sanctuary Sweeper",      "Reach Level 45.",                           MISSION_LEVEL,          45,  850 },
    { "Incense Burner",         "Read 120 chapters.",                        MISSION_BIBLE_CHAPTER,  120, 900 },
    { "Wick Trimmer",           "Read 130 chapters.",                        MISSION_BIBLE_CHAPTER,  130, 950 },
    { "Oil Supplier",           "Log 45 prayers.",                           MISSION_PRAYER,         45,  1000 },
    { "Lamp Lighter",           "Reach Level 50.",                           MISSION_LEVEL,          50,  1200 },
    { "Bookbinder",             "Read 140 chapters.",                        MISSION_BIBLE_CHAPTER,  140, 1250 },
    { "Stylus Carver",          "Read 150 chapters.",                        MISSION_BIBLE_CHAPTER,  150, 1300 },
    { "Papyrus Scraper",        "Log 50 prayers.",                           MISSION_PRAYER,         50,  1350 },
    { "Ink Prepared",           "Reach Level 55.",                           MISSION_LEVEL,          55,  1400 },
    { "Scribe's Apprentice",    "Read 160 chapters.",                        MISSION_BIBLE_CHAPTER,  160, 1450 },
    { "Scroll Archivist",       "Read 170 chapters.",                        MISSION_BIBLE_CHAPTER,  170, 1500 },
    { "Tithes Accountant",      "Log 55 prayers.",                           MISSION_PRAYER,         55,  1550 },
    { "Offering Gatherer",      "Reach Level 60.",                           MISSION_LEVEL,          60,  1600 },
    { "Altar Attendant",        "Read 180 chapters.",                        MISSION_BIBLE_CHAPTER,  180, 1650 },
    { "Temple Cleaner",         "Read 190 chapters.",                        MISSION_BIBLE_CHAPTER,  190, 1700 },
    { "Wall Surveyor",          "Log 60 prayers.",                           MISSION_PRAYER,         60,  1750 },
    { "Gate Watcher",           "Reach Level 65.",                           MISSION_LEVEL,          65,  1800 },
    { "Field Scout",            "Read 200 chapters.",                        MISSION_BIBLE_CHAPTER,  200, 1850 },
    { "Harvest Assistant",      "Read 210 chapters.",                        MISSION_BIBLE_CHAPTER,  210, 1900 },
    { "Weeder of Temptation",   "Log 65 prayers.",                           MISSION_PRAYER,         65,  1950 },
    { "Waterer of Growth",      "Reach Level 70.",                           MISSION_LEVEL,          70,  2000 },
    { "Seed Planter",           "Read 220 chapters.",                        MISSION_BIBLE_CHAPTER,  220, 2050 },
    { "Fruit Producer",         "Read 230 chapters.",                        MISSION_BIBLE_CHAPTER,  230, 2100 },
    { "Root Deepener",          "Log 70 prayers.",                           MISSION_PRAYER,         70,  2150 },
    { "Branch Keeper",          "Reach Level 75.",                           MISSION_LEVEL,          75,  2200 },
    { "Vine Tender",            "Read 240 chapters.",                        MISSION_BIBLE_CHAPTER,  240, 2250 },
    { "Sheepfold Guard",        "Read 250 chapters.",                        MISSION_BIBLE_CHAPTER,  250, 2300 },
    { "Fisher of Men",          "Log 75 prayers.",                           MISSION_PRAYER,         75,  2350 },
    { "City on a Hill",         "Reach Level 80.",                           MISSION_LEVEL,          80,  2400 },
    { "Salt of the Earth",      "Read 260 chapters.",                        MISSION_BIBLE_CHAPTER,  260, 2450 },
    { "Light Bearer",           "Read 270 chapters.",                        MISSION_BIBLE_CHAPTER,  270, 2500 },
    { "Truth Seeker",           "Log 80 prayers.",                           MISSION_PRAYER,         80,  2550 },
    { "Grace Receiver",         "Reach Level 85.",                           MISSION_LEVEL,          85,  2600 },
    { "Law Keeper",             "Read 280 chapters.",                        MISSION_BIBLE_CHAPTER,  280, 2650 },
    { "Wisdom Seeker",          "Read 290 chapters.",                        MISSION_BIBLE_CHAPTER,  290, 2700 },
    { "Psalms Composer",        "Log 85 prayers.",                           MISSION_PRAYER,         85,  2750 },
    { "Gospel Proclaimer",      "Reach Level 90.",                           MISSION_LEVEL,          90,  2800 },
    { "Epistle Examiner",       "Read 300 chapters.",                        MISSION_BIBLE_CHAPTER,  300, 2850 },
    { "Chronicle Enthusiast",   "Read 310 chapters.",                        MISSION_BIBLE_CHAPTER,  310, 2900 },
    { "Reader of Prophets",     "Log 90 prayers.",                           MISSION_PRAYER,         90,  2950 },
    { "Meditator on Law",       "Reach Level 95.",                           MISSION_LEVEL,          95,  3000 },
    { "Disciple of Solitude",   "Read 320 chapters.",                        MISSION_BIBLE_CHAPTER,  320, 3100 },
    { "Walker in Stillness",    "Read 330 chapters.",                        MISSION_BIBLE_CHAPTER,  330, 3200 },
    { "Practitioner of Fasting","Log 95 prayers.",                           MISSION_PRAYER,         95,  3300 },
    { "Memorizer of Truth",     "Reach Level 100.",                          MISSION_LEVEL,          100, 3500 },
    { "Diligent Student",       "Read 340 chapters.",                        MISSION_BIBLE_CHAPTER,  340, 3600 },
    { "Constant Worshipper",    "Read 350 chapters.",                        MISSION_BIBLE_CHAPTER,  350, 3700 },
    { "Devoted Intercessor",    "Log 100 prayers.",                          MISSION_PRAYER,         100, 3800 },
    { "Master of Self-Control", "Reach Level 105.",                          MISSION_LEVEL,          105, 4000 },
    { "Exemplar of Gentleness", "Read 360 chapters.",                        MISSION_BIBLE_CHAPTER,  360, 4100 },
    { "Model of Faithfulness",  "Read 370 chapters.",                        MISSION_BIBLE_CHAPTER,  370, 4200 },
    { "Practitioner of Patience","Log 110 prayers.",                         MISSION_PRAYER,         110, 4300 },
    { "Spreader of Kindness",   "Reach Level 110.",                          MISSION_LEVEL,          110, 4500 },
    { "Bearer of Joy",          "Read 380 chapters.",                        MISSION_BIBLE_CHAPTER,  380, 4600 },
    { "Provider of Comfort",    "Read 390 chapters.",                        MISSION_BIBLE_CHAPTER,  390, 4700 },
    { "Minister of Mercy",      "Log 120 prayers.",                          MISSION_PRAYER,         120, 4800 },
    { "Healer of Divisions",    "Reach Level 115.",                          MISSION_LEVEL,          115, 5000 },
    { "Peacemaker in Conflict", "Read 400 chapters.",                        MISSION_BIBLE_CHAPTER,  400, 5100 },
    { "Advocate of Justice",    "Read 410 chapters.",                        MISSION_BIBLE_CHAPTER,  410, 5200 },
    { "Organizer of Ministry",  "Log 130 prayers.",                          MISSION_PRAYER,         130, 5300 },
    { "Coordinator of Fellowship","Reach Level 120.",                        MISSION_LEVEL,          120, 5500 },
    { "Leader of Small Groups", "Read 420 chapters.",                        MISSION_BIBLE_CHAPTER,  420, 5600 },
    { "Teacher of Foundations", "Read 430 chapters.",                        MISSION_BIBLE_CHAPTER,  430, 5700 },
    { "Counselor of Wisdom",    "Log 140 prayers.",                          MISSION_PRAYER,         140, 5800 },
    { "Voice for the Voiceless", "Reach Level 125.",                         MISSION_LEVEL,          125, 6000 },
    { "Restorer of Broken Paths","Read 440 chapters.",                       MISSION_BIBLE_CHAPTER,  440, 6100 },
    { "Shepherd of the Lost",   "Read 450 chapters.",                        MISSION_BIBLE_CHAPTER,  450, 6200 },
    { "Harvester of the Harvest","Log 150 prayers.",                         MISSION_PRAYER,         150, 6300 },
    { "Rebuilder of Ancient Ruins","Reach Level 130.",                       MISSION_LEVEL,          130, 7500 }
};
const int TOTAL_MISSIONS = sizeof(MISSIONS) / sizeof(MISSIONS[0]);
int questProgress = 0; // number of missions completed so far; MISSIONS[questProgress] is the active one

// Bible Reader state - only ONE chapter is ever held in memory at a time
int bibleBookIndex = 0;     // 0 = Genesis
int bibleChapter   = 1;
String bibleChapterText = "";
std::vector<String> bibleLines;
int bibleCurrentPage = 0;
int bibleTotalPages  = 1;
bool bibleChapterXPClaimed = false;

// ================= WI-FI SETTINGS =================
// Credentials live only in flash (NVS), never in the compiled binary.
String savedSSID = "";
String savedPassword = "";
String settingsSSID = "";
String settingsPassword = "";
int settingsField = 0;          // 0 = editing SSID, 1 = editing Password
const int APP_MAX_SSID_LEN = 32;    // 802.11 SSID limit
const int APP_MAX_PASS_LEN = 63;    // WPA2 passphrase limit

// Function Prototypes
void drawHome();
void drawReader();
void drawSearch();
void drawJournalView();
void drawJournalWrite();
void drawQuests();
void drawBibleReader();
void drawSettings();
void drawCredits();
void updateHeader(String title);
void updateFooter(String msg);
bool connectToWiFi(String ssid, String pass, unsigned long timeoutMs);
void fetchScripture(String ref);
void fetchBibleChapter(int bookIdx, int chapter);
void bibleNextPage();
void biblePrevPage();
void savePrayer(String text);
String getSpiritualTitle(int level);
void addXP(int amount);
void checkDailyReset();
int missionCurrentValue(const Mission& m);
void checkQuestProgress();
std::vector<String> wrapTextToLines(const String& text, int maxWidth);
void printWordWrapped(const String& text, int x, int y, int maxWidth, int maxLines);
String scrolledTail(const String& text, int maxWidth);

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(COLOR_BG);

    // Initialize Flash Memory for Prayers & Stats
    if (!LittleFS.begin(true)) {
        updateFooter("LittleFS Mount Failed! Journal disabled.");
    }

    prefs.begin("holy_os", false);
    userXP = prefs.getInt("xp", 0);
    userLevel = prefs.getInt("lvl", 1);
    dailyReadCount = prefs.getInt("today", 0);
    lastLoginDay = prefs.getInt("day", -1);
    currentMannaIndex = prefs.getInt("manna", 0);

    totalReadCount = prefs.getInt("totalRead", 0);
    totalPrayerCount = prefs.getInt("totalPray", 0);
    totalBibleChaptersRead = prefs.getInt("totalBibleCh", 0);
    questProgress = prefs.getInt("questProg", 0);
    if (questProgress < 0) questProgress = 0;
    if (questProgress > TOTAL_MISSIONS) questProgress = TOTAL_MISSIONS;
    checkQuestProgress(); // in case stats already clear the next threshold(s)

    // Resume Bible reading position (defaults to Genesis 1 on first run)
    bibleBookIndex = prefs.getInt("bBook", 0);
    bibleChapter   = prefs.getInt("bChap", 1);
    if (bibleBookIndex < 0 || bibleBookIndex >= TOTAL_BIBLE_BOOKS) bibleBookIndex = 0;
    if (bibleChapter < 1 || bibleChapter > BIBLE_BOOKS[bibleBookIndex].chapters) bibleChapter = 1;

    // Load saved Wi-Fi credentials from flash (NVS) - never from source code.
    savedSSID = prefs.getString("ssid", "");
    savedPassword = prefs.getString("pass", "");

    if (savedSSID.length() == 0) {
        // Fresh device / freshly-flashed shared .bin: no network configured yet.
        // Send the user straight to Settings instead of failing silently.
        statusMessage = "Welcome! Enter your Wi-Fi to get started.";
        settingsSSID = "";
        settingsPassword = "";
        settingsField = 0;
        currentState = STATE_SETTINGS;
        drawSettings();
        return;
    }

    updateHeader("HOLY SPIRIT OS - CONNECTING...");
    connectToWiFi(savedSSID, savedPassword, 6000);

    delay(500);
    drawHome();
}

void loop() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        // Global Navigation Override: backtick/tilde always returns Home.
        // NOTE: status.del (Backspace) is intentionally NOT part of this check
        // anymore - it used to hijack Backspace and send the user Home instead
        // of deleting a character while typing.
        bool wantsHome = false;
        for (auto key : status.word) {
            if (key == '`' || key == '~') wantsHome = true;
        }
        if (wantsHome) {
            if (currentState != STATE_HOME) {
                currentState = STATE_HOME;
                statusMessage = "Returned to Sanctuary.";
                drawHome();
            }
            delay(15);
            return;
        }

        // State-Specific Input Handling
        switch (currentState) {
            case STATE_HOME:
                for (auto key : status.word) {
                    if (key == '1') { currentState = STATE_READER; fetchScripture(SPIRIT_VERSES[currentMannaIndex]); drawReader(); }
                    else if (key == '2') { currentState = STATE_SEARCH; inputBuffer = ""; drawSearch(); }
                    else if (key == '3') { currentState = STATE_JOURNAL_VIEW; drawJournalView(); }
                    else if (key == '4') { currentState = STATE_JOURNAL_WRITE; inputBuffer = ""; drawJournalWrite(); }
                    else if (key == '5') { currentState = STATE_QUESTS; drawQuests(); }
                    else if (key == '6') {
                        currentState = STATE_BIBLE_READ;
                        if (bibleChapterText.length() == 0) {
                            fetchBibleChapter(bibleBookIndex, bibleChapter);
                        }
                        drawBibleReader();
                    }
                    else if (key == '7') {
                        currentState = STATE_SETTINGS;
                        settingsSSID = savedSSID;
                        settingsPassword = savedPassword;
                        settingsField = 0;
                        drawSettings();
                    }
                }
                break;

            case STATE_READER:
                for (auto key : status.word) {
                    if (key == 'c' || key == 'C') { // Complete passage / Gain XP
                        if (!readerXPClaimed) {
                            readerXPClaimed = true;
                            dailyReadCount++;
                            prefs.putInt("today", dailyReadCount);
                            totalReadCount++;
                            prefs.putInt("totalRead", totalReadCount);
                            addXP(dailyReadCount == dailyGoal ? 200 : 35);
                            statusMessage = (dailyReadCount == dailyGoal) ? "DAILY MISSION COMPLETED! +200 XP!" : "Word Received! +35 XP!";
                            checkQuestProgress();
                        } else {
                            statusMessage = "Already claimed! Press [SPACE] for the next Word.";
                        }
                        drawReader();
                    }
                    else if (key == 'w' || key == 'W') { if (readerScrollLine > 0) { readerScrollLine--; drawReader(); } }
                    else if (key == 's' || key == 'S') {
                        int maxScroll = max(0, (int)activeScriptureLines.size() - LINES_PER_PAGE);
                        if (readerScrollLine < maxScroll) { readerScrollLine++; drawReader(); }
                    }
                }
                if (status.space) { // Next Pentecostal Manna
                    currentMannaIndex = (currentMannaIndex + 1) % TOTAL_SPIRIT_VERSES;
                    prefs.putInt("manna", currentMannaIndex);
                    fetchScripture(SPIRIT_VERSES[currentMannaIndex]);
                    readerScrollLine = 0;
                    drawReader();
                }
                break;

            case STATE_SEARCH:
            case STATE_JOURNAL_WRITE: {
                bool changed = false;

                if (status.del) { // Backspace - now correctly wired to status.del
                    if (inputBuffer.length() > 0) {
                        inputBuffer.remove(inputBuffer.length() - 1);
                        changed = true;
                    }
                }

                if (status.enter) { // Enter/OK - now correctly wired to status.enter
                    if (inputBuffer.length() > 0) {
                        if (currentState == STATE_SEARCH) {
                            currentState = STATE_READER;
                            readerScrollLine = 0;
                            fetchScripture(inputBuffer);
                            drawReader();
                        } else {
                            savePrayer(inputBuffer);
                            addXP(50);
                            checkQuestProgress();
                            currentState = STATE_JOURNAL_VIEW;
                            drawJournalView();
                        }
                    }
                    delay(15);
                    return; // state already changed & redrawn above
                }

                for (auto key : status.word) {
                    if (key >= 32 && key <= 126) { // Printable ASCII
                        if (inputBuffer.length() < 120) {
                            inputBuffer += (char)key;
                            changed = true;
                        }
                    }
                }

                if (changed) {
                    if (currentState == STATE_SEARCH) drawSearch();
                    else drawJournalWrite();
                }
                break;
            }

            case STATE_JOURNAL_VIEW:
            case STATE_QUESTS:
                if (status.space) { currentState = STATE_HOME; drawHome(); }
                break;

            case STATE_BIBLE_READ:
                for (auto key : status.word) {
                    if (key == 'b' || key == 'B') { biblePrevPage(); drawBibleReader(); }
                    else if (key == 'c' || key == 'C') {
                        if (!bibleChapterXPClaimed) {
                            bibleChapterXPClaimed = true;
                            dailyReadCount++;
                            prefs.putInt("today", dailyReadCount);
                            totalReadCount++;
                            prefs.putInt("totalRead", totalReadCount);
                            totalBibleChaptersRead++;
                            prefs.putInt("totalBibleCh", totalBibleChaptersRead);
                            addXP(dailyReadCount == dailyGoal ? 200 : 35);
                            statusMessage = (dailyReadCount == dailyGoal) ? "DAILY MISSION COMPLETED! +200 XP!" : "Chapter Received! +35 XP!";
                            checkQuestProgress();
                        } else {
                            statusMessage = "Already claimed for this chapter.";
                        }
                        drawBibleReader();
                    }
                }
                if (status.space) { bibleNextPage(); drawBibleReader(); }
                break;

            case STATE_SETTINGS: {
                bool changed = false;
                bool onTextField = (settingsField == 0 || settingsField == 1);
                String* activeBuf = (settingsField == 0) ? &settingsSSID : &settingsPassword;
                int maxLen = (settingsField == 0) ? APP_MAX_SSID_LEN : APP_MAX_PASS_LEN;

                if (status.tab) { // Cycle SSID -> Password -> Credits -> SSID...
                    settingsField = (settingsField + 1) % 3;
                    changed = true;
                }

                if (status.del && onTextField) { // Backspace on the active text field
                    if (activeBuf->length() > 0) {
                        activeBuf->remove(activeBuf->length() - 1);
                        changed = true;
                    }
                }

                if (status.enter) {
                    if (settingsField == 2) { // Credits row selected - navigate, don't save
                        currentState = STATE_CREDITS;
                        drawCredits();
                    } else { // Save Wi-Fi & attempt to connect
                        savedSSID = settingsSSID;
                        savedPassword = settingsPassword;
                        prefs.putString("ssid", savedSSID);
                        prefs.putString("pass", savedPassword);
                        connectToWiFi(savedSSID, savedPassword, 8000);
                        drawSettings();
                    }
                    delay(15);
                    return;
                }

                if (onTextField) {
                    for (auto key : status.word) {
                        if (key >= 32 && key <= 126) { // Printable ASCII
                            if ((int)activeBuf->length() < maxLen) {
                                *activeBuf += (char)key;
                                changed = true;
                            }
                        }
                    }
                }

                if (changed) drawSettings();
                break;
            }

            case STATE_CREDITS:
                if (status.space) { currentState = STATE_SETTINGS; drawSettings(); }
                break;
        }
    }
    delay(15);
}

// ================= GAMIFICATION & XP ENGINE =================

String getSpiritualTitle(int level) {
    if (level >= 300) return "Apostle of the Nations";
    if (level >= 290) return "Patriarch of Truth";
    if (level >= 280) return "Elder of the Sanctuary";
    if (level >= 270) return "Watchman on the Wall";
    if (level >= 260) return "Pillar of the Church";
    if (level >= 250) return "Ambassador of Heaven";
    if (level >= 240) return "Herald of Righteousness";
    if (level >= 230) return "Messenger of Grace";
    if (level >= 220) return "Voice in the Wilderness";
    if (level >= 210) return "Shield of Faith";
    if (level >= 200) return "Sword of the Spirit";
    if (level >= 190) return "Defender of the Gospel";
    if (level >= 180) return "Guardian of the Covenant";
    if (level >= 170) return "Steward of the Mysteries";
    if (level >= 160) return "Follower of the Way";
    if (level >= 150) return "Disciple of Wisdom";
    if (level >= 140) return "Witness of the Light";
    if (level >= 130) return "Servant of the Most High";
    if (level >= 120) return "Steadfast Believer";
    if (level >= 110) return "Sower of Seeds";
    if (level >= 100) return "Bearer of the Cross";
    if (level >= 90)  return "Warrior of Light";
    if (level >= 80)  return "Faithful Steward";
    if (level >= 70)  return "Scholar of Scripture";
    if (level >= 60)  return "Champion of Hope";
    if (level >= 50)  return "Torchbearer";
    if (level >= 45)  return "Soldier of the Cross";
    if (level >= 40)  return "Knight of the Spirit";
    if (level >= 35)  return "Commander of Prayer";
    if (level >= 30)  return "General of the Faith";
    if (level >= 25)  return "Captain of the Word";
    if (level >= 20)  return "Revivalist";
    if (level >= 15)  return "Prophetic Voice";
    if (level >= 10)  return "Anointed Vessel";
    if (level >= 5)   return "Prayer Warrior";
    if (level >= 3)   return "Spirit-Filled Believer";
    return "Seeker of the Word";
}

void addXP(int amount) {
    userXP += amount;
    int xpNeeded = userLevel * 250;
    if (userXP >= xpNeeded) {
        userLevel++;
        userXP -= xpNeeded;
        statusMessage = "HALLELUJAH! Leveled up to LVL " + String(userLevel) + "!";
    }
    prefs.putInt("xp", userXP);
    prefs.putInt("lvl", userLevel);
}

void checkDailyReset() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    int today = timeinfo.tm_yday;
    if (today != lastLoginDay && lastLoginDay != -1) {
        dailyReadCount = 0;
        prefs.putInt("today", 0);
        statusMessage = "New Day! Daily Quests reset. Seek Him early!";
    }
    lastLoginDay = today;
    prefs.putInt("day", today);
}

// Returns how far the player currently is toward the given mission's goal.
int missionCurrentValue(const Mission& m) {
    switch (m.type) {
        case MISSION_READ:          return totalReadCount;
        case MISSION_PRAYER:        return totalPrayerCount;
        case MISSION_LEVEL:         return userLevel;
        case MISSION_BIBLE_CHAPTER: return totalBibleChaptersRead;
    }
    return 0;
}

// Checks the currently-active Discipleship Mission against lifetime stats.
// Call this after any action that could move a mission's counter (claiming
// Daily Word XP, claiming a Bible chapter, or logging a prayer). Missions
// unlock strictly in order - completing one reveals the next automatically,
// and a single action can cascade through several completions at once.
void checkQuestProgress() {
    while (questProgress < TOTAL_MISSIONS) {
        Mission m = MISSIONS[questProgress];
        int currentVal = missionCurrentValue(m);

        if (currentVal >= m.threshold) {
            questProgress++;
            prefs.putInt("questProg", questProgress);
            addXP(m.xpReward);
            statusMessage = "MISSION COMPLETE: " + String(m.title) + "! +" + String(m.xpReward) + " XP";
        } else {
            break;
        }
    }
}

// ================= WI-FI CONNECTION =================

// Attempts to join the given network, blocking up to timeoutMs. Used both
// at boot (with saved credentials) and from the Settings screen (with
// whatever the user just typed). Updates statusMessage with the result.
bool connectToWiFi(String ssid, String pass, unsigned long timeoutMs) {
    if (ssid.length() == 0) {
        statusMessage = "No Wi-Fi network set. Go to [7] Settings.";
        return false;
    }

    updateFooter("Connecting to " + ssid + "...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
        checkDailyReset();
        statusMessage = "Wi-Fi Connected to " + ssid + "! Anointing active.";
        return true;
    } else {
        statusMessage = "Couldn't join " + ssid + ". Check password & retry.";
        return false;
    }
}

// ================= BIBLE API & STORAGE =================

void fetchScripture(String ref) {
    readerXPClaimed = false; // a new passage means a fresh chance to claim XP

    if (WiFi.status() != WL_CONNECTED) {
        activeReference = ref;
        activeScripture = "Error: Wi-Fi offline. Please connect to receive heavenly data.";
        activeScriptureLines = wrapTextToLines(activeScripture, CONTENT_WIDTH);
        return;
    }

    updateFooter("Downloading Scripture from heaven...");
    HTTPClient http;

    // Format reference for URL (replace spaces with '+')
    String urlRef = ref;
    urlRef.replace(" ", "+");
    String url = "https://bible-api.com/" + urlRef + "?translation=kjv";

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.containsKey("text")) {
            activeReference = doc["reference"].as<String>();
            activeScripture = doc["text"].as<String>();
            activeScripture.trim();
            statusMessage = "Word received! Press [C] to Claim XP.";
        } else {
            activeReference = ref;
            activeScripture = "Scripture not found. Try format: 'John 3:16' or 'Acts 2'";
            statusMessage = "Search error. Check reference spelling.";
        }
    } else {
        activeScripture = "Connection failed. HTTP Code: " + String(httpCode);
    }
    http.end();

    activeScriptureLines = wrapTextToLines(activeScripture, CONTENT_WIDTH);
}

// Downloads exactly ONE chapter (never the whole Bible / whole book) and
// paginates it locally. Paging within the chapter costs no further network
// calls; a new fetch only happens once you page past what's cached here.
void fetchBibleChapter(int bookIdx, int chapter) {
    if (bookIdx < 0) bookIdx = 0;
    if (bookIdx >= TOTAL_BIBLE_BOOKS) bookIdx = TOTAL_BIBLE_BOOKS - 1;

    if (WiFi.status() != WL_CONNECTED) {
        bibleChapterText = "Error: Wi-Fi offline. Connect to Wi-Fi to read the Bible.";
        bibleLines = wrapTextToLines(bibleChapterText, CONTENT_WIDTH);
        bibleTotalPages = 1;
        bibleCurrentPage = 0;
        bibleBookIndex = bookIdx;
        bibleChapter = chapter;
        return;
    }

    updateFooter("Downloading " + String(BIBLE_BOOKS[bookIdx].name) + " " + String(chapter) + "...");

    HTTPClient http;
    String bookUrl = String(BIBLE_BOOKS[bookIdx].name);
    bookUrl.replace(" ", "+");
    String url = "https://bible-api.com/" + bookUrl + "+" + String(chapter) + "?translation=kjv";

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.containsKey("verses")) {
            JsonArray verses = doc["verses"].as<JsonArray>();
            String built = "";
            for (JsonObject v : verses) {
                int vn = v["verse"].as<int>();
                String vt = v["text"].as<String>();
                vt.trim();
                built += String(vn) + " " + vt + "  ";
            }
            bibleChapterText = built;
            statusMessage = "Loaded " + String(BIBLE_BOOKS[bookIdx].name) + " " + String(chapter) + ". Press [C] for XP.";
        } else {
            bibleChapterText = "Could not load this chapter. Press [SPACE]/[B] to try another.";
        }
    } else {
        bibleChapterText = "Connection failed. HTTP Code: " + String(httpCode);
    }
    http.end();

    bibleBookIndex = bookIdx;
    bibleChapter = chapter;
    bibleLines = wrapTextToLines(bibleChapterText, CONTENT_WIDTH);
    bibleTotalPages = max(1, (int)((bibleLines.size() + LINES_PER_PAGE - 1) / LINES_PER_PAGE));
    bibleCurrentPage = 0;
    bibleChapterXPClaimed = false;

    prefs.putInt("bBook", bibleBookIndex);
    prefs.putInt("bChap", bibleChapter);
}

void bibleNextPage() {
    if (bibleCurrentPage + 1 < bibleTotalPages) {
        bibleCurrentPage++;
        return;
    }
    // Out of local pages - need the next chapter (one network call).
    int nextChapter = bibleChapter + 1;
    int nextBook = bibleBookIndex;
    if (nextChapter > BIBLE_BOOKS[bibleBookIndex].chapters) {
        nextBook++;
        nextChapter = 1;
        if (nextBook >= TOTAL_BIBLE_BOOKS) {
            statusMessage = "You've reached the end of Revelation! Looping to Genesis.";
            nextBook = 0;
        }
    }
    fetchBibleChapter(nextBook, nextChapter);
}

void biblePrevPage() {
    if (bibleCurrentPage > 0) {
        bibleCurrentPage--;
        return;
    }
    int prevChapter = bibleChapter - 1;
    int prevBook = bibleBookIndex;
    if (prevChapter < 1) {
        prevBook--;
        if (prevBook < 0) {
            statusMessage = "You're at the very beginning: Genesis 1.";
            return;
        }
        prevChapter = BIBLE_BOOKS[prevBook].chapters;
    }
    fetchBibleChapter(prevBook, prevChapter);
    bibleCurrentPage = bibleTotalPages - 1;
}

void savePrayer(String text) {
    File file = LittleFS.open("/prayers.txt", FILE_APPEND);
    if (file) {
        struct tm timeinfo;
        String timeStr = "Prayer:";
        if (getLocalTime(&timeinfo)) {
            char buf[20];
            strftime(buf, sizeof(buf), "%m/%d %H:%M", &timeinfo);
            timeStr = String(buf);
        }
        file.println("[" + timeStr + "] " + text);
        file.close();
        totalPrayerCount++;
        prefs.putInt("totalPray", totalPrayerCount);
        statusMessage = "Prayer sealed in flash memory & sent to heaven! +50 XP";
    } else {
        statusMessage = "Error: Could not save prayer to flash.";
    }
}

// ================= UI DRAWING ROUTINES =================

void updateHeader(String title) {
    if (title.length() > 36) title = title.substring(0, 33) + "...";
    M5Cardputer.Display.fillRect(0, 0, 240, 18, COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_GOLD, COLOR_HEADER);
    M5Cardputer.Display.setCursor(4, 5);
    M5Cardputer.Display.print(title);
}

void updateFooter(String msg) {
    M5Cardputer.Display.fillRect(0, 120, 240, 15, COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_TEXT, COLOR_HEADER);
    M5Cardputer.Display.setCursor(2, 124);

    if (msg.length() > 38) msg = msg.substring(0, 35) + "...";
    M5Cardputer.Display.print(msg);
}

void drawHome() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("SANCTUARY DASHBOARD  [~] Home");

    // Player Spiritual Status
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(6, 20);
    M5Cardputer.Display.print("LVL " + String(userLevel) + ": " + getSpiritualTitle(userLevel));

    // XP Progress Bar
    int xpNeeded = userLevel * 250;
    int barWidth = map(userXP, 0, xpNeeded, 0, 150);
    M5Cardputer.Display.drawRect(6, 29, 152, 7, COLOR_INK);
    M5Cardputer.Display.fillRect(7, 30, barWidth, 5, COLOR_XP_BAR);
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(164, 29);
    M5Cardputer.Display.print(String(userXP) + "/" + String(xpNeeded) + " XP");

    // Daily Mission Badge
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(6, 39);
    M5Cardputer.Display.print("Daily Manna: " + String(dailyReadCount) + "/" + String(dailyGoal) + " read today");

    // Navigation Menu
    int menuY = 52;
    const int menuSpacing = 9;

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY);  M5Cardputer.Display.print("[1]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Daily Spirit Word (Reader)");

    menuY += menuSpacing;
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY);  M5Cardputer.Display.print("[2]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Scripture Search");

    menuY += menuSpacing;
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY);  M5Cardputer.Display.print("[3]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" View Prayer Altar (Log)");

    menuY += menuSpacing;
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY);  M5Cardputer.Display.print("[4]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Log a Prayer");

    menuY += menuSpacing;
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY); M5Cardputer.Display.print("[5]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Discipleship Missions ");
    M5Cardputer.Display.setTextColor(questProgress >= TOTAL_MISSIONS ? COLOR_XP_BAR : COLOR_MUTED);
    M5Cardputer.Display.print("(" + String(questProgress) + "/" + String(TOTAL_MISSIONS) + ")");

    menuY += menuSpacing;
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY); M5Cardputer.Display.print("[6]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Read The Bible");

    menuY += menuSpacing;
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, menuY); M5Cardputer.Display.print("[7]");
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.print(" Wi-Fi Settings ");
    M5Cardputer.Display.setTextColor(wifiUp ? COLOR_XP_BAR : COLOR_ACCENT);
    M5Cardputer.Display.print(wifiUp ? "(Connected)" : "(Offline)");

    updateFooter(statusMessage);
}

void drawReader() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("WORD: " + activeReference);

    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setClipRect(0, CONTENT_Y, 240, CONTENT_H);
    int cursorY = CONTENT_Y + 2;
    int startLine = readerScrollLine;
    int endLine = min((int)activeScriptureLines.size(), startLine + LINES_PER_PAGE + 1);
    for (int i = startLine; i < endLine; i++) {
        M5Cardputer.Display.setCursor(CONTENT_X, cursorY);
        M5Cardputer.Display.print(activeScriptureLines[i]);
        cursorY += LINE_HEIGHT;
    }
    M5Cardputer.Display.clearClipRect();

    M5Cardputer.Display.fillRect(0, 108, 240, 12, COLOR_BG);
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(4, 109);
    M5Cardputer.Display.print("[SPACE] Next | [C] XP | [W/S] Scroll");

    updateFooter(statusMessage);
}

void drawSearch() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("SCRIPTURE SEARCH  [~] Home");

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 30);
    M5Cardputer.Display.print("Enter Verse Reference or Topic:");
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, 42);
    M5Cardputer.Display.print("e.g., 'Acts 2:4', 'Psalm 23', 'John 14'");

    // Input Box (shows a scrolled tail so long text never runs off the edge)
    M5Cardputer.Display.fillRect(7, 61, 226, 22, COLOR_BG);
    M5Cardputer.Display.drawRect(6, 60, 228, 24, COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(10, 68);
    M5Cardputer.Display.print(scrolledTail(inputBuffer, 214) + "_");

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 95);
    M5Cardputer.Display.print("Press [ENTER] to search the Scriptures.");

    updateFooter("Type reference, [ENTER] to search, [DEL] to fix.");
}

void drawJournalWrite() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("PRAYER ALTAR - WRITE  [~] Home");

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 25);
    M5Cardputer.Display.print("Pour out your heart to the Lord:");

    // Input Area
    M5Cardputer.Display.fillRect(7, 39, 226, 60, COLOR_BG);
    M5Cardputer.Display.drawRect(6, 38, 228, 62, COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setClipRect(7, 39, 226, 60);
    printWordWrapped(inputBuffer + "_", 10, 43, 216, 5);
    M5Cardputer.Display.clearClipRect();

    M5Cardputer.Display.setTextColor(COLOR_XP_BAR);
    M5Cardputer.Display.setCursor(6, 105);
    M5Cardputer.Display.print("Press [ENTER] to save prayer (+50 XP)");

    updateFooter("Write prayer, [ENTER] to save, [DEL] to fix.");
}

void drawJournalView() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("PRAYER ALTAR - HISTORY  [~] Home");

    File file = LittleFS.open("/prayers.txt", FILE_READ);
    if (!file || file.size() == 0) {
        M5Cardputer.Display.setTextColor(COLOR_MUTED);
        M5Cardputer.Display.setCursor(10, 50);
        M5Cardputer.Display.print("No prayers logged yet. Go to [4] to write!");
    } else {
        // Read last 480 bytes to show most recent prayers
        if (file.size() > 480) file.seek(file.size() - 480);
        String logData = file.readString();
        file.close();

        M5Cardputer.Display.setTextColor(COLOR_INK);
        M5Cardputer.Display.setClipRect(0, CONTENT_Y, 240, CONTENT_H);
        printWordWrapped(logData, 4, CONTENT_Y + 2, 232, LINES_PER_PAGE + 1);
        M5Cardputer.Display.clearClipRect();
    }

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 108);
    M5Cardputer.Display.print("Press [SPACE] or [~] to return Home.");

    updateFooter("Reviewing answered & pending prayers.");
}

void drawQuests() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("DISCIPLESHIP MISSIONS  [~] Home");

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 20);
    M5Cardputer.Display.print("Missions Complete: " + String(questProgress) + "/" + String(TOTAL_MISSIONS));

    if (questProgress >= TOTAL_MISSIONS) {
        // Every mission finished - show a closing message instead of a card.
        M5Cardputer.Display.setTextColor(COLOR_XP_BAR);
        M5Cardputer.Display.setCursor(6, 38);
        M5Cardputer.Display.print("ALL MISSIONS COMPLETE!");

        M5Cardputer.Display.setTextColor(COLOR_INK);
        M5Cardputer.Display.setClipRect(0, CONTENT_Y, 240, CONTENT_H);
        printWordWrapped("Well done, good and faithful servant. You have walked every step of this Discipleship Path.", 6, 52, 228, 4);
        M5Cardputer.Display.clearClipRect();

        updateFooter("Press [SPACE] or [~] to return Home.");
        return;
    }

    Mission m = MISSIONS[questProgress];
    int currentVal = missionCurrentValue(m);
    if (currentVal > m.threshold) currentVal = m.threshold;

    // Mission title
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 33);
    M5Cardputer.Display.print("-> " + String(m.title));

    // Mission description (wraps up to 2 lines)
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setClipRect(0, 44, 240, 22);
    printWordWrapped(String(m.description), 6, 45, 228, 2);
    M5Cardputer.Display.clearClipRect();

    // Progress bar
    int barWidth = map(currentVal, 0, m.threshold, 0, 160);
    M5Cardputer.Display.drawRect(6, 67, 160, 9, COLOR_INK);
    M5Cardputer.Display.fillRect(7, 68, barWidth, 7, COLOR_XP_BAR);
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(170, 68);
    M5Cardputer.Display.print(String(currentVal) + "/" + String(m.threshold));

    // Reward
    M5Cardputer.Display.setTextColor(COLOR_XP_BAR);
    M5Cardputer.Display.setCursor(6, 82);
    M5Cardputer.Display.print("Reward: +" + String(m.xpReward) + " XP");

    // Next mission teaser
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, 95);
    if (questProgress + 1 < TOTAL_MISSIONS) {
        M5Cardputer.Display.print("Next: " + String(MISSIONS[questProgress + 1].title));
    } else {
        M5Cardputer.Display.print("This is the final mission!");
    }

    updateFooter("Press [SPACE] or [~] to return Home.");
}

void drawBibleReader() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    String hdr = String(BIBLE_BOOKS[bibleBookIndex].name) + " " + String(bibleChapter) +
                 " (" + String(bibleCurrentPage + 1) + "/" + String(bibleTotalPages) + ")";
    updateHeader(hdr);

    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setClipRect(0, CONTENT_Y, 240, CONTENT_H);
    int cursorY = CONTENT_Y + 2;
    int startLine = bibleCurrentPage * LINES_PER_PAGE;
    int endLine = min((int)bibleLines.size(), startLine + LINES_PER_PAGE);
    for (int i = startLine; i < endLine; i++) {
        M5Cardputer.Display.setCursor(CONTENT_X, cursorY);
        M5Cardputer.Display.print(bibleLines[i]);
        cursorY += LINE_HEIGHT;
    }
    M5Cardputer.Display.clearClipRect();

    M5Cardputer.Display.fillRect(0, 108, 240, 12, COLOR_BG);
    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(4, 109);
    M5Cardputer.Display.print("[SPACE] Next | [B] Prev | [C] XP");

    updateFooter(statusMessage);
}

void drawSettings() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("WI-FI SETTINGS  [~] Home");

    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    M5Cardputer.Display.setTextColor(wifiUp ? COLOR_XP_BAR : COLOR_ACCENT);
    M5Cardputer.Display.setCursor(6, 20);
    M5Cardputer.Display.print(wifiUp ? ("Status: Connected (" + WiFi.SSID() + ")") : "Status: Offline");

    // --- SSID field ---
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, 31);
    M5Cardputer.Display.print("Network Name (SSID):");

    bool ssidActive = (settingsField == 0);
    M5Cardputer.Display.fillRect(7, 41, 226, 14, COLOR_BG);
    M5Cardputer.Display.drawRect(6, 40, 228, 16, ssidActive ? COLOR_GOLD : COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(10, 45);
    M5Cardputer.Display.print(scrolledTail(settingsSSID, 214) + (ssidActive ? "_" : ""));

    // --- Password field ---
    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, 60);
    M5Cardputer.Display.print("Password:");

    bool passActive = (settingsField == 1);
    M5Cardputer.Display.fillRect(7, 70, 226, 14, COLOR_BG);
    M5Cardputer.Display.drawRect(6, 69, 228, 16, passActive ? COLOR_GOLD : COLOR_HEADER);
    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(10, 74);
    String masked = "";
    for (size_t i = 0; i < settingsPassword.length(); i++) masked += '*';
    M5Cardputer.Display.print(scrolledTail(masked, 214) + (passActive ? "_" : ""));

    // --- Credits row (selectable, not a text field) ---
    bool creditsActive = (settingsField == 2);
    M5Cardputer.Display.setTextColor(creditsActive ? COLOR_GOLD : COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, 93);
    M5Cardputer.Display.print(String(creditsActive ? "> " : "  ") + "View Credits & About");

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, 103);
    M5Cardputer.Display.print("[TAB] Switch  [ENTER] Select/Save");

    updateFooter(statusMessage);
}

void drawCredits() {
    M5Cardputer.Display.fillScreen(COLOR_BG);
    updateHeader("CREDITS & ABOUT  [~] Home");

    M5Cardputer.Display.setTextColor(COLOR_INK);
    int y = 22;
    const int lineGap = 12;

    M5Cardputer.Display.setTextColor(COLOR_GOLD);
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("Holy Spirit OS");
    y += lineGap;

    M5Cardputer.Display.setTextColor(COLOR_INK);
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("Created by M5RogueOps");
    y += lineGap;
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("GitHub: github.com/M5RogueOps");
    y += lineGap;
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("Web: ethicalhackersden.org");
    y += lineGap + 2;

    M5Cardputer.Display.setTextColor(COLOR_MUTED);
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("Scripture data: bible-api.com");
    y += lineGap;
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("JSON parsing: ArduinoJson lib");
    y += lineGap;
    M5Cardputer.Display.setCursor(6, y); M5Cardputer.Display.print("Hardware: M5Stack Cardputer");

    updateFooter("Press [SPACE] or [~] to go back.");
}

// Splits text into a list of lines, each of which is guaranteed to fit
// within maxWidth pixels at the current font/size. Shared by every screen
// so wrapping behaves identically everywhere.
std::vector<String> wrapTextToLines(const String& text, int maxWidth) {
    std::vector<String> lines;
    int maxCharsPerLine = max(1, maxWidth / CHAR_WIDTH);

    String currentLine = "";
    String word = "";

    for (int i = 0; i <= (int)text.length(); i++) {
        char c = (i < (int)text.length()) ? text[i] : ' ';
        bool isBreak = (c == ' ' || c == '\n' || i == (int)text.length());

        if (isBreak) {
            if (word.length() > 0) {
                // If the word itself is longer than a whole line, hard-split it.
                while ((int)word.length() > maxCharsPerLine) {
                    if (currentLine.length() > 0) {
                        lines.push_back(currentLine);
                        currentLine = "";
                    }
                    lines.push_back(word.substring(0, maxCharsPerLine));
                    word = word.substring(maxCharsPerLine);
                }

                int prospectiveLen = currentLine.length() + (currentLine.length() > 0 ? 1 : 0) + word.length();
                if (prospectiveLen > maxCharsPerLine) {
                    lines.push_back(currentLine);
                    currentLine = word;
                } else {
                    if (currentLine.length() > 0) currentLine += " ";
                    currentLine += word;
                }
                word = "";
            }

            if (c == '\n') {
                lines.push_back(currentLine);
                currentLine = "";
            }
        } else {
            word += c;
        }
    }

    if (currentLine.length() > 0) lines.push_back(currentLine);
    if (lines.empty()) lines.push_back("");
    return lines;
}

// Draws up to maxLines of word-wrapped text starting at (x,y). Callers that
// need to guarantee no overdraw onto neighboring UI (header/footer/input
// boxes) should wrap this call in setClipRect(...)/clearClipRect().
void printWordWrapped(const String& text, int x, int y, int maxWidth, int maxLines) {
    std::vector<String> lines = wrapTextToLines(text, maxWidth);
    int cursorY = y;
    int drawn = 0;
    for (size_t i = 0; i < lines.size() && drawn < maxLines; i++, drawn++) {
        M5Cardputer.Display.setCursor(x, cursorY);
        M5Cardputer.Display.print(lines[i]);
        cursorY += LINE_HEIGHT;
    }
}

// Returns the tail-end of `text` that fits within maxWidth pixels, so live
// input boxes show what the user just typed instead of running off-screen.
String scrolledTail(const String& text, int maxWidth) {
    int maxChars = max(1, maxWidth / CHAR_WIDTH);
    if ((int)text.length() <= maxChars) return text;
    return text.substring(text.length() - maxChars);
}
