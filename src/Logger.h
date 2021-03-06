/*
 * This file is part of the bitcoin-classic project
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LOGITEM_H
#define LOGITEM_H

#include <sstream>
#include <iomanip>

#include "tinyformat.h"

extern bool fDebug;

static const bool DEFAULT_LOGTIMESTAMPS = true;

namespace Log {

class Item;
class SilentItem;
class Channel;
class ManagerPrivate;

enum Verbosity {
    DebugLevel = 1,
    WarningLevel,
    InfoLevel,
    CriticalLevel,
    FatalLevel
};

/**
 * @brief The Sections enum is a way to enable/disable logging by sections of code.
 * Sections in logging work in two levels. We have groups with enum 0, 1000, 2000, etc.
 * And under the groups are a small set of named sub-sections.
 *
 * Calling logInfo(2020) will use section "Net" and an unnamed sub-section 2020.
 * A user can enable/disable whole sections, like "Net" and that would make this log item
 * be marked as disabled. Or the user can disable/enable specific sections like 2020 to
 * only have his/her class be logged.
 *
 * By not passing any section to logInfo() you select the group Global, which is typically
 * used for general end-user information regardless of where in the code it is located.
 */
enum Sections {
    Global = 0,

    Validation = 1000,
    BlockValidation,
    Bench,
    Prune,

    Networking = 2000,
    Net,
    Addrman,
    Proxy,
    NWM,
    Tor,
    ThinBlocks,
    ExpeditedBlocks,

    AdminServer = 3000,
    RPC,
    LibEvent,
    HTTP,
    ZMQ,

    DB = 4000,
    Coindb,

    Wallet = 5000,
    SelectCoins,

    Internals = 6000,
    Mempool,
    MempoolRej,
    Random,

    QtGui = 7000
};

/**
 * @brief The Manager class is meant to be a singleton that owns the logging settings.
 * This class actually distributes the logging lines to the different channels and
 * it allows a log-item to know it has been disabled and as such can avoid actual logging.
 */
class Manager
{
public:
    /// please use instance() instead.
    Manager();
    ~Manager();

    /**
     * Returns true if the section has been enabled.
     */
    bool isEnabled(short section, Log::Verbosity verbosity) const;
    /**
     * Returns the mapping of an old-style text based section to our internal one.
     */
    short section(const char *category);

    static Manager* instance();

    /// This is only called by the Item to log its data on the logging channels.
    void log(Item *item);

    /// Request files to be closed and opened anew.
    void reopenLogFiles();

    /// Load a simple setup that prints all logging to stdout and nothing to file.
    void loadDefaultTestSetup();

    /**
     * @brief parseConfig reads the config file logging.conf from the datadir.
     * The config file is a list of channels to output to and the log sections with
     * log-verbosity.
     * Example file;
     *
     * <code>
     * channel file
     * # comment
     * channel console
     *     option linenumber false
     *     option methodname true
     *     option filename true
     *
     * # Log sections from Log::Sections and verbosity
     * 0 quiet
     * 1000 info
     * 1001 debug
     * </endcode>
     */
    void parseConfig();

    static const std::string &sectionString(short section);

private:
    void clearChannels();

    ManagerPrivate *d;
};


/// Items are instantiated by calling macros like logInfo(), use this to stream your data into the item.
/// One item instance represents one line in the logs, a linefeed is auto-appended if needed.
class Item
{
public:
    explicit Item(const char *filename, int lineNumber, const char *methodName, short section, int verbosity = InfoLevel);
    explicit Item(int verbosity = InfoLevel);
    Item(const Item &other);
    ~Item();

    inline Item &nospace() { d->space = false; return *this; }
    inline Item &space() { d->space = true; d->stream << ' '; return *this; }
    inline Item &maybespace() { if (d->space) d->stream << ' '; return *this; }
    inline bool useSpace() const { return d->space; }

    /// returns the verbosity that this item has been instantiated with.
    /// @see Log::Verbosity
    inline int verbosity() const {
        return d->verbosity;
    }
    /// Returns true if the logging is enabled for this item.
    /// It is safe to skip more expensive operations if the item is disabled.
    inline bool isEnabled() const {
        return d->on;
    }
    /// The application Section this log item was created in. @see Log::Sections
    short section() const;

    inline Item &operator<<(bool val) { if(d->on)d->stream << (val?"true":"false"); return maybespace(); }
    inline Item &operator<<(char c) { if(d->on)d->stream << c; return maybespace(); }
    inline Item &operator<<(const char *c) { if(d->on)d->stream << c; return maybespace(); }
    inline Item &operator<<(int i) { if(d->on)d->stream << i; return maybespace(); }
    inline Item &operator<<(double val) { if(d->on)d->stream << val; return maybespace(); }
    inline Item &operator<<(const std::string &str) { if(d->on){d->stream << '"' << str << '"';} return maybespace(); }
    inline Item &operator<<(short val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(unsigned short val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(unsigned int val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(long val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(unsigned long val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(long long val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(unsigned long long val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(float val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(long double val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(const void* val) { if(d->on)d->stream << val ; return maybespace(); }
    inline Item &operator<<(std::nullptr_t) { if(d->on)d->stream << "(nullptr)" ; return maybespace(); }
    inline Item &operator<<(std::ostream& (*pf)(std::ostream&)) { if(d->on)d->stream << pf; return *this; }
    inline Item &operator<<(std::ios& (*pf)(std::ios&)) { if(d->on)d->stream << pf; return *this; }
    inline Item &operator<<(std::ios_base& (*pf)(std::ios_base&)) { if(d->on)d->stream << pf; return *this; }

    Item operator=(const Item &other) = delete;

private:
    friend class Manager;
    friend class MessageLogger;
    struct State {
        State(const char*filename, int lineNumber, const char *methodName, short section) : section(section), lineNum(lineNumber), filename(filename), methodName(methodName) {}
        std::ostringstream stream;
        bool space;
        bool on;
        short verbosity;
        short ref;
        short section;
        const int lineNum;
        const char *filename;
        const char *methodName;
    } *d;
};


/// This class should likely never be used manually, it purely exists to create Item and SilentItem instances.
class MessageLogger {
public:
    MessageLogger();
    explicit MessageLogger(const char *filename, int lineNumber, const char *methodName);

    inline Item debug(short section = 0) {
        return Log::Item(m_file, m_line, m_method, section, Log::DebugLevel);
    }
    inline Item warning(short section = 0) {
        return Log::Item(m_file, m_line, m_method, section, Log::WarningLevel);
    }
    inline Item info(short section = 0) {
        return Log::Item(m_file, m_line, m_method, section, Log::InfoLevel);
    }
    inline Item critical(short section = 0) {
        return Log::Item(m_file, m_line, m_method, section, Log::CriticalLevel);
    }
    inline Item fatal(short section = 0) {
        return Log::Item(m_file, m_line, m_method, section, Log::FatalLevel);
    }

    inline Log::SilentItem noDebug(int = 0);

#define MAKE_LOGGER_FUNCTIONS(n) \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline void infoCompat(const char *section, const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Manager::instance()->section(section)); \
        if (format && item.isEnabled()) \
            item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline Item debug(const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Log::Global, DebugLevel); \
        item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
        return item; \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline Item info(const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Log::Global, InfoLevel); \
        item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
        return item; \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline Item warning(const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Log::Global, WarningLevel); \
        item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
        return item; \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline Item critical(const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Log::Global, CriticalLevel); \
        item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
        return item; \
    } \
    template<TINYFORMAT_ARGTYPES(n)> \
    inline Item fatal(const char* format, TINYFORMAT_VARARGS(n)) { \
        Item item(m_file, m_line, m_method, Log::Global, FatalLevel); \
        item.d->stream << tfm::format(format, TINYFORMAT_PASSARGS(n)); \
        return item; \
    }
TINYFORMAT_FOREACH_ARGNUM(MAKE_LOGGER_FUNCTIONS)

    inline void infoCompat(const char *section, const char* format = 0) {
        Item item(m_file, m_line, m_method, Manager::instance()->section(section));
        item.d->stream << format;
    }
    inline Item debug(const char* format) {
        Item item(m_file, m_line, m_method, Log::Global, DebugLevel);
        item.d->stream << format;
        return item;
    }
    inline Item info(const char* format) {
        Item item(m_file, m_line, m_method, Log::Global);
        item.d->stream << format;
        return item;
    }
    inline Item warning(const char* format) {
        Item item(m_file, m_line, m_method, Log::Global, WarningLevel);
        item.d->stream << format;
        return item;
    }
    inline Item critical(const char* format) {
        Item item(m_file, m_line, m_method, Log::Global, CriticalLevel);
        item.d->stream << format;
        return item;
    }
    inline Item fatal(const char* format) {
        Item item(m_file, m_line, m_method, Log::Global, FatalLevel);
        item.d->stream << format;
        return item;
    }

private:
    const int m_line;
    const char *m_file, *m_method;
};


/// A no-logging item.
class SilentItem {
public:
    inline int verbosity() const {
        return FatalLevel;
    }
    inline SilentItem &nospace() { return *this; }
    inline SilentItem &space() { return *this; }
    inline SilentItem &maybespace() { return *this; }
    inline SilentItem &operator<<(char) { return *this; }
    inline SilentItem &operator<<(const char *) {  return *this; }
    inline SilentItem &operator<<(int) {  return *this; }
    inline SilentItem &operator<<(double) { return *this; }
    inline SilentItem &operator<<(const std::string&) { return *this; }
    inline SilentItem &operator<<(bool) { return *this; }
    inline SilentItem &operator<<(short) { return *this; }
    inline SilentItem &operator<<(unsigned short) { return *this; }
    inline SilentItem &operator<<(unsigned int) { return *this; }
    inline SilentItem &operator<<(long) {  return *this; }
    inline SilentItem &operator<<(unsigned long) { return *this; }
    inline SilentItem &operator<<(long long) {  return *this; }
    inline SilentItem &operator<<(unsigned long long) { return *this; }
    inline SilentItem &operator<<(float) {  return *this; }
    inline SilentItem &operator<<(long double) {  return *this; }
    inline SilentItem &operator<<(void*) { return *this; }
    inline SilentItem &operator<<(std::nullptr_t) { return *this; }
    inline SilentItem &operator<<(std::ostream& (*pf)(std::ostream&)) { return *this; }
    inline SilentItem &operator<<(std::ios& (*pf)(std::ios&)) { return *this; }
    inline SilentItem &operator<<(std::ios_base& (*pf)(std::ios_base&)) { return *this; }
};

inline SilentItem MessageLogger::noDebug(int) { return SilentItem(); }

} // namespace Log

#ifdef BTC_LOGCONTEXT
  #define BTC_MESSAGELOG_FILE __FILE__
  #define BTC_MESSAGELOG_LINE __LINE__
  #define BTC_MESSAGELOG_FUNC __PRETTY_FUNCTION__
#else
  #define BTC_MESSAGELOG_FILE nullptr
  #define BTC_MESSAGELOG_LINE 0
  #define BTC_MESSAGELOG_FUNC nullptr
#endif

#define logDebug Log::MessageLogger(BTC_MESSAGELOG_FILE, BTC_MESSAGELOG_LINE, BTC_MESSAGELOG_FUNC).debug
#define logInfo Log::MessageLogger(BTC_MESSAGELOG_FILE, BTC_MESSAGELOG_LINE, BTC_MESSAGELOG_FUNC).info
#define logWarning Log::MessageLogger(BTC_MESSAGELOG_FILE, BTC_MESSAGELOG_LINE, BTC_MESSAGELOG_FUNC).warning
#define logCritical Log::MessageLogger(BTC_MESSAGELOG_FILE, BTC_MESSAGELOG_LINE, BTC_MESSAGELOG_FUNC).critical
#define logFatal Log::MessageLogger(BTC_MESSAGELOG_FILE, BTC_MESSAGELOG_LINE, BTC_MESSAGELOG_FUNC).fatal

#define BTC_NO_DEBUG_MACRO while (false) Log::MessageLogger().noDebug

#if defined(BTC_NO_DEBUG_OUTPUT)
#  undef logDebug
#  define logDebug BTC_NO_DEBUG_MACRO
#endif
#if defined(BTC_NO_INFO_OUTPUT)
#  undef logInfo
#  define logInfo BTC_NO_DEBUG_MACRO
#endif
#if defined(BTC_NO_WARNING_OUTPUT)
#  undef logWarning
#  define logWarning BTC_NO_DEBUG_MACRO
#endif

#include <atomic>
template<class T>
inline Log::Item operator<<(Log::Item item, const std::atomic<T> &atomic) {
    if (item.isEnabled()) item << atomic.load();
    return item.maybespace();
}
template<class T>
inline Log::SilentItem operator<<(Log::SilentItem item, const std::atomic<T>&) { return item; }
Log::Item operator<<(Log::Item item, const std::exception &ex);
inline Log::SilentItem operator<<(Log::SilentItem item, const std::exception &ex) { return item; }

#include <vector>
template<class V>
inline Log::Item operator<<(Log::Item item, const std::vector<V> &vector) {
    if (item.isEnabled()) {
        const bool old = item.useSpace();
        item.nospace() << '(';
        for (size_t i = 0; i < vector.size(); ++i) { if (i) item << ','; item << vector[i]; }
        item << ')';
        if (old)
            return item.space();
    }
    return item.maybespace();
}
template<class V>
inline Log::SilentItem operator<<(Log::SilentItem item, const std::vector<V>&) { return item; }


#endif
