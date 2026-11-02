#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <ctime>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>
#include <charconv>
#include "spsc.h"

enum class Level : uint8_t {
    Debug, 
    Info,
    Warn,
    Error
};

enum class Arg_Type : uint8_t {
    Int32,
    Int64,
    UInt64,
    Double,
    Char,
    String,
};

struct LogRecordHeader {
    int64_t timestamp;
    const char* format;
    uint64_t payload_len;
    uint8_t arg_count;
    Level level;
};

struct alignas(64) LogRecord {
    static constexpr std::size_t Size = 256;
    static constexpr std::size_t PayloadSize = Size - sizeof(LogRecordHeader);
    
    LogRecordHeader hdr;
    std::byte payload[PayloadSize];
};

static_assert(sizeof(LogRecord) == LogRecord::Size, "LogRecord must be 256 bytes");
static_assert(std::is_trivially_copyable_v<LogRecord>, "LogRecord must be memcpy-safe");


class Logger {
private:
    SPSC<LogRecord, 1 << 14> q_;
    
    static int64_t now_ticks() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    template<typename... Args>
    static void encode_args(LogRecord& rec, const Args&... args) {
        (encode(rec, args) && ...);
    }

    static bool encode(LogRecord& rec, int32_t v) {
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::Int32)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &v, sizeof(v));
        rec.hdr.payload_len += sizeof(v);
        
        rec.hdr.arg_count += 1; 
        return true;
    }
    
    static bool encode(LogRecord& rec, int64_t v) {
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::Int64)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &v, sizeof(v));
        rec.hdr.payload_len += sizeof(v);

        rec.hdr.arg_count += 1;
        return true;
    }
        
    static bool encode(LogRecord& rec, uint64_t v) {
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::UInt64)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &v, sizeof(v));
        rec.hdr.payload_len += sizeof(v);

        rec.hdr.arg_count += 1;
        return true;
    }

    static bool encode(LogRecord& rec, double v) {
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::Double)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &v, sizeof(v));
        rec.hdr.payload_len += sizeof(v);

        rec.hdr.arg_count += 1;
        return true;
    }
    
    static bool encode(LogRecord& rec, char v) {
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::Char)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &v, sizeof(v));
        rec.hdr.payload_len += sizeof(v);

        rec.hdr.arg_count += 1;
        return true;
    }
        
        
    static bool encode(LogRecord& rec, std::string_view v) {
        std::size_t room = LogRecord::PayloadSize - rec.hdr.payload_len - 3;
        uint16_t len = static_cast<uint16_t>(std::min(v.size(), room));
        
        rec.payload[rec.hdr.payload_len] = std::byte{static_cast<uint8_t>(Arg_Type::String)};
        rec.hdr.payload_len += 1;
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], &len, sizeof(len));
        rec.hdr.payload_len += sizeof(len);
        
        std::memcpy(&rec.payload[rec.hdr.payload_len], v.data(), len);
        rec.hdr.payload_len += len;
        
        rec.hdr.arg_count += 1;
        return true;
    }
    
    void run() {
        while (running_.load(std::memory_order_acquire)) {
            std::optional<LogRecord> r = q_.pop();
            if (r.has_value()) {
                format_record(r.value());
                flush_buffer();
            }
        }
        
        while (true) {
            std::optional<LogRecord> r = q_.pop();
            if (!r.has_value()) break;
            format_record(r.value());
            flush_buffer();
        }
    }


    static std::string_view level_name(Level lvl) {
        switch(lvl) {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warn: return "WARN";
            case Level::Error: return "ERROR";
        }
        return "?????";
    }  
    
    void format_record(const LogRecord& rec) {
        line_len_ = 0;
            
        append_timestamp(rec.hdr.timestamp);
        append(" ");
        append(level_name(rec.hdr.level));
        append(" ");

        std::string_view fmt(rec.hdr.format);
        std::size_t pos = 0;
        int args_left = rec.hdr.arg_count;
        
        for (std::size_t i = 0; i < fmt.size(); i++) {
            bool is_placeholder = (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}');
            
            if (is_placeholder) {
                if (args_left > 0) {
                    append_arg(rec, pos);
                    args_left -= 1;
                } else {
                    append("{}");
                } 
                i++;
            } else {
                append(fmt.substr(i, 1));
            }
        }
        
        line_[line_len_] = '\n';
        line_len_ += 1;
    }
                
    void append_timestamp(int64_t ticks) {
        std::time_t seconds = static_cast<std::time_t>(ticks / 1'000'000'000);
        long micros = static_cast<long>((ticks % 1'000'000'000) / 1'000);
        
        std::tm tm;
        localtime_r(&seconds, &tm);
        
        char buf[40];
        int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06ld", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, micros);
        append(std::string_view(buf, n));
    }

    template<typename T>
    void append_number(T v) {
        char buf[32];
        std::to_chars_result result = std::to_chars(buf, buf + sizeof(buf), v);
        std::size_t length = result.ptr - buf;
        append(std::string_view(buf, length));
    } 
    
    void append_arg(const LogRecord& rec, std::size_t& pos) {
        Arg_Type type = static_cast<Arg_Type>(rec.payload[pos]);
        pos += 1;
        
        switch (type) {
            case Arg_Type::Int32: {
                int32_t v;
                std::memcpy(&v, &rec.payload[pos], sizeof(v));
                pos += sizeof(v);
                append_number(v);
                break;  
            }
            case Arg_Type::Int64: {
                int64_t v;
                std::memcpy(&v, &rec.payload[pos], sizeof(v));
                pos += sizeof(v);
                append_number(v);
                break;  
            }
            case Arg_Type::UInt64: {
                uint64_t v;
                std::memcpy(&v, &rec.payload[pos], sizeof(v));
                pos += sizeof(v);
                append_number(v);
                break;  
            }
            case Arg_Type::Double: {
                double v;
                std::memcpy(&v, &rec.payload[pos], sizeof(v));
                pos += sizeof(v);
                append_number(v);
                break;  
            }
            case Arg_Type::Char: {
                char c;
                std::memcpy(&c, &rec.payload[pos], sizeof(c));
                pos += sizeof(c);
                append(std::string_view(&c, 1));
                break;
            }
            case Arg_Type::String: {
                uint16_t len;
                std::memcpy(&len, &rec.payload[pos], sizeof(len));
                pos += sizeof(len);
                
                const char* chars = reinterpret_cast<const char*>(&rec.payload[pos]);
                append(std::string_view(chars, len));
                pos += len;
                break;
            }
        }
    }
    
    
    void append(std::string_view s) {
        std::size_t room = sizeof(line_) - 1 - line_len_;
        std::size_t n = std::min(s.size(), room);
        std::memcpy(&line_[line_len_], s.data(), n);
        line_len_ += n;
    } 
        
    void flush_buffer() {
        std::fwrite(line_, 1, line_len_, file_);
        std::fflush(file_);
    };
    
    std::atomic<bool> running_{false};
    std::atomic<Level> level_{Level::Info};
    std::atomic<uint64_t> dropped_{0};

    std::thread worker_;
    std::string path_;
    FILE* file_ = nullptr;
    char line_[1024];
    std::size_t line_len_ = 0;


public:
    explicit Logger(const std::string& file_path) {
        file_ = std::fopen(file_path.c_str(), "w");
    }
        
    ~Logger() {
        stop();
        if (file_ != nullptr) std::fclose(file_);     
    } 
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void init() {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&Logger::run, this);
    }

    void stop() {
        bool was_running = running_.exchange(false);
        if (!was_running) return;
        worker_.join();
    } 
    
    template<typename... Args>    
    void log(Level lvl, const char* format, const Args&... args) {
        if (lvl < level_.load(std::memory_order_relaxed)) return;
        
        LogRecord rec;
        rec.hdr.timestamp = now_ticks();    
        rec.hdr.format = format;
        rec.hdr.payload_len = 0;
        rec.hdr.arg_count = 0;
        rec.hdr.level = lvl;
    
        encode_args(rec, args...);
        
        bool pushed = q_.push(rec);
        if (!pushed) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void set_level(Level lvl) {
        level_.store(lvl, std::memory_order_relaxed);
    }
    
    Level level() const {
        return level_.load(std::memory_order_relaxed);
    }

    uint64_t dropped_count() const {
        return dropped_.load(std::memory_order_relaxed);
    }
};

int main() {
    Logger lg("app.log");
    lg.init();

    std::string user = "ananya";
    lg.log(Level::Info, "order {} sym {} qty {} px {} side {} by {}",
           1001, "AAPL", 500, 189.25, 'B', std::string_view(user));
    lg.log(Level::Debug, "this is filtered out {}", 1);
    lg.log(Level::Warn, "big number {}", static_cast<int64_t>(123456789012345));

    for (int i = 0; i < 10'000; ++i)
        lg.log(Level::Info, "tick {} price {}", i, 100.0 + i * 0.01);

    lg.stop();
    std::cout << "dropped: " << lg.dropped_count() << "\n";
}



