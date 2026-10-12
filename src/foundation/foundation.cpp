#include "ribterra/foundation.hpp"
#include "skill_balance_generated.h"
#include <array>
#include <limits>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace ribterra {
namespace {
std::string hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        value += digits[byte >> 4];
        value += digits[byte & 15];
    }
    return value;
}
} // namespace
std::string sha256(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> result{};
    unsigned int length{};
    if (EVP_Digest(bytes.data(), bytes.size(), result.data(), &length, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA256 failed");
    return hex(std::span(result.data(), length));
}
std::string default_content_hash() {
    // This exact recipe manifest identifies the existing rules. Changes must change this hash.
    static const std::string rules =
        std::string("ribterra:rules7:chunks32:bounds-96,160:surface,mine:portal28,24:startvision16.5:entt-stable-id:tick20:"
                    "pathqueue1:work4096:deadline1200:restarts3:house30,15,0:farm20,10,5:stockpile15,5,0:"
                    "physical-inventory1:carry10:camp10000:stockpile5000:yield3,2,2:farm8per400:food-"
                    "unreserved-storage:tasks4096:reservations8192:stacks65536:logistics128:scan4096:"
                    "taskdeadline10000:work-intents1:intents8192:visits128:admissions32:backoff2,4,8,16,32:"
                    "attempts64:admissiondeadline1200:builderreplacement1:topology-work-retry1:professions1:"
                    "professionvisits1024:professionadmissions32:evaluate10:manualorders1:orders8192:"
                    "queue8:history256:manualvisits1500:manualadmissions32:partialpaths1:"
                    "gather-until-exhausted-or-one-delivery:protected-cargo-boundary:cancel-order-distinct-"
                    "from-release:coop6-nations-lobby:manual-hold600:wilderness1:species4:bounded-expeditions:night-per-task5:rations10:spears:hide:economy1:recipes15:deliveries:upgrades:road-command-disabled:road-speed-bonus-disabled:desire-path-wear1:terrain-profile-alpine-valley1:terrain-rules1:nightworkers:settlement1:nightpause:nightcost5:youthcost10:recruit50,2400,3600:explicit-extraction-only1:default-one-delivery:controlleases1:"
                    "disconnectgrace30s:weather1:profile-temperate-valley1:calendar06:day28800:"
                    "winter-offset864000:cover650:splitmix64-fold5249425745415448:purposes1-11:"
                    "splitmix-add9e3779b97f4a7c15-mulbf58476d1ce4e5b9-mul94d049bb133111eb-shifts30,27,31:"
                    "fold-seed-profile-algorithm-scenario-day-purpose-period:"
                    "purpose-background,daily,event-presence,storm,start,duration,intensity,wind-direction,wind-strength,first-start,first-duration:"
                    "temperature-integer-toward0:anomaly600:connected-equal-events-earliest-wind:"
                    "kind-anomaly-clear10-cloud0-precip-minus20-storm-minus60:forced-priority-storm-precip-background:"
                    "forecast4-exhaustive28800:cache6:intervals8-generated7:"
                    "seasonal80,180,70,-100:daily-anomaly-minus40-plus40:"
                    "civil-diurnal-minus25,-30,-35,-40,-45,-50,-50,-40,-25,-10,5,20,30,40,45,50,45,35,20,5,-5,-10,-15,-20:"
                    "precip350,200,400,500:storm100,100,150,200:clear550,700,450,350:"
                    "wind100-300,200-450,350-650,700-1000:generic-civil02-20:duration2-4h,4-6h:"
                    "intensity400-750,800-1000:first-storm-start9600-14400-duration4800-7200:"
                    "snow100-plus-intensity400:melt40-plus-temp8-cap800:rain-melt120:"
                    "wet-rain600-plus-intensity600:dry-warm120-cold30-plus-wind120:"
                    "signed-cover-remainder28800:work10000,9500,9000,8000,7500:cold9500,8500:"
                    "snow-rate-clamp-minus1000-plus1000:wet-rate-clamp-minus1000-plus1200:cover0-1000-outward-remainder0:"
                    "cold-threshold-minus100-minus200:min-weather-cold-floor7500:reason-weather-wins-tie:"
                    "activity-keyed-remainder10000:surface-gather-build-only:no-wait-move-underground-xp:"
                    "first-winter-food-max100-living120:camp-capacity-max10000-80wood40stone-food:"
                    "initial-capacity-bound180120-pernation12120:skills1:") +
        std::string(skill_balance::canonical_json);
    static const auto digest = sha256({reinterpret_cast<const std::uint8_t *>(rules.data()), rules.size()});
    return digest;
}
std::string secure_token(std::size_t bytes) {
    if (bytes < 16 || bytes > 64)
        throw std::invalid_argument("Token size outside bounds");
    std::vector<unsigned char> random(bytes);
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1)
        throw std::runtime_error("Secure random generation failed");
    return hex(random);
}
bool constant_time_equal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
bool valid_utf8_text(std::string_view text, std::size_t maximum_bytes, bool multiline) {
    if (text.empty() || text.size() > maximum_bytes)
        return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) {
            if (first < 0x20 && !(multiline && (first == '\n' || first == '\t')))
                return false;
            if (first == 0x7f)
                return false;
            continue;
        }
        int continuation{};
        std::uint32_t value{}, minimum{};
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
            value = first & 0x1f;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            value = first & 0x0f;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            value = first & 0x07;
            minimum = 0x10000;
        } else
            return false;
        if (i + static_cast<std::size_t>(continuation) > text.size())
            return false;
        for (int n = 0; n < continuation; ++n) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
        if ((value >= 0x80 && value <= 0x9f) || (value >= 0x202a && value <= 0x202e) ||
            (value >= 0x2066 && value <= 0x2069))
            return false;
    }
    return true;
}
} // namespace ribterra
