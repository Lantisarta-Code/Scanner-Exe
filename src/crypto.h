// SHA-256, HMAC, a small big-number type and SRP-6a - the same protocol as the Python version,
// so the Android app talks to this program unchanged.
#pragma once
#include "common.h"

namespace ec {

string sha256(const string& data);
string hmac_sha256(const string& key, const string& data);
bool same_text(const string& a, const string& b);         // compares in constant time

class BigInt {
public:
    BigInt() = default;
    explicit BigInt(uint32_t v) { if (v) d_.push_back(v); }
    static BigInt from_bytes(const string& be);
    static BigInt from_hex(const string& hex);
    string to_bytes(size_t len) const;                      // big-endian, left-padded to len
    string to_hex() const;                                  // lower case, no leading zeros ("0" for 0)
    bool is_zero() const { return d_.empty(); }
    int cmp(const BigInt& o) const;
    BigInt add(const BigInt& o) const;
    BigInt sub(const BigInt& o) const;                      // *this >= o
    BigInt mul(const BigInt& o) const;
    BigInt mod(const BigInt& m) const;
    static BigInt modpow(BigInt base, const BigInt& exp, const BigInt& m);
    size_t bits() const;

private:
    void trim();
    vector<uint32_t> d_;                                    // little-endian limbs
    friend void divmod(const BigInt& a, const BigInt& b, BigInt* q, BigInt* r);
};

// ---- SRP (RFC 5054 2048-bit group, g = 2, SHA-256) ----
extern const BigInt& SRP_N();
const int SRP_LEN = 256;
BigInt srp_x(const string& salt, const string& pin);
struct SrpHello {
    string salt;
    BigInt b, B, v;
};
SrpHello srp_server_hello(const string& pin);
string srp_server_key(const BigInt& A, const BigInt& b, const BigInt& B, const BigInt& v);
std::pair<string, string> srp_proofs(const string& K, const BigInt& A, const BigInt& B);   // M1, M2

struct Keys {
    string enc, mac;
};
string stream_xor(const string& kenc, const string& direction, uint64_t n, const string& data);
json seal(const Keys& k, const string& direction, uint64_t n, const json& obj);
json unseal(const Keys& k, const string& direction, uint64_t n, const json& msg);   // throws on a bad signature
string be64(uint64_t n);

bool valid_pin(const string& pin);
string new_pin();

}  // namespace ec
