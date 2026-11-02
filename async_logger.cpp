#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstddef>
#include <type_traits>
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
    SPSC<LogRecord, 2**16> q_;
    
    static int64_t now_ticks() {
        std::chrono::duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    }
    
    // template<typename T, typename... Args>
    // static void encode_args(LogRecord& rec, const T& head, const Args&... args) {
    //     encode(rec, head);
    //     encode_args(rec, args);
    // }
    

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
        uint16_6 len = static_cast<uint16_t>(std::min(v.size(), room));
        
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
        if (running_) {
            while(LogRecord r = q_.pop()) {
                format_record(r);
            }
        }
    }
    
    void format_record(const LogRecord& rec) {
        memcpy(line_, std::to_char(rec.timestamp), 64);
        line_ += 64;
        strcpy(line_, rec.format);
        line_ += strlen(rec.format);
        memcpy(line_, std::to_char(rec.payload_len), 64);
        line_ += 64;
        memcpy(line_, std::to_char(rec.arg_count), 8);
        line_ += 8;
        memcpy(line_, std::to_char(rec.level), 8);
        line_ += 8;
        
    }
        
    
     
    void append_timestamp(int64_t ticks);
    const std::byte* append_arg(const std::byte* p);
    void append(std::string_view s);
    void flush_buffer();
    
    std::atomic<bool> running_{false};
    std::atomic<Level> level_{Level::Info};
    std::atomic<uint64_t> dropped_{0};

    std::thread worker_;
    std::string path_;
    File* file_ = nullptr;
    char line_[1024];
    std::size_t line_len_ = 0;


public:
    explicit Logger(const std::string& file_path) {
        file_ = fopen(file_path.c_str(), "wa");
    }
        
    ~Logger() {}; 
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void init() {
        worker_.run(run);
        worker_.join();     
        running_ = true;
    }

    void stop() {
        running_ = false;
    } 
    
    template<typename... Args>    
    void log(Level lvl, const char* format, const Args&... args);
    
    void set_level(Level lvl);
    
    Level level() const;

    uint64_t dropped_count() const;
};



