#include "crypto.h"

#include <algorithm>
#include <cstdio>

namespace ec {

// ---------------------------------------------------------------- SHA-256 / HMAC
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

static inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

string sha256(const string& data) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    string m = data;
    uint64_t bits = (uint64_t)data.size() * 8;
    m += char(0x80);
    while (m.size() % 64 != 56) m += char(0);
    for (int i = 7; i >= 0; i--) m += char((bits >> (i * 8)) & 0xFF);
    uint32_t w[64];
    for (size_t o = 0; o < m.size(); o += 64) {
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)(unsigned char)m[o + i * 4] << 24) | ((uint32_t)(unsigned char)m[o + i * 4 + 1] << 16) |
                   ((uint32_t)(unsigned char)m[o + i * 4 + 2] << 8) | (uint32_t)(unsigned char)m[o + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    string out(32, '\0');
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = char((h[i] >> (24 - 8 * j)) & 0xFF);
    return out;
}

string hmac_sha256(const string& key0, const string& data) {
    string key = key0.size() > 64 ? sha256(key0) : key0;
    key.resize(64, '\0');
    string ip(64, '\0'), op(64, '\0');
    for (int i = 0; i < 64; i++) {
        ip[i] = char(key[i] ^ 0x36);
        op[i] = char(key[i] ^ 0x5c);
    }
    return sha256(op + sha256(ip + data));
}

bool same_text(const string& a, const string& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

// ---------------------------------------------------------------- BigInt
void BigInt::trim() {
    while (!d_.empty() && d_.back() == 0) d_.pop_back();
}
BigInt BigInt::from_bytes(const string& be) {
    BigInt r;
    r.d_.assign((be.size() + 3) / 4, 0);
    for (size_t i = 0; i < be.size(); i++) {
        size_t pos = be.size() - 1 - i;                      // byte i from the end
        r.d_[i / 4] |= (uint32_t)(unsigned char)be[pos] << (8 * (i % 4));
    }
    r.trim();
    return r;
}
BigInt BigInt::from_hex(const string& hex) { return from_bytes(ec::from_hex(hex)); }
string BigInt::to_bytes(size_t len) const {
    string out(std::max(len, d_.size() * 4), '\0');
    for (size_t i = 0; i < d_.size() * 4; i++) out[out.size() - 1 - i] = char((d_[i / 4] >> (8 * (i % 4))) & 0xFF);
    if (out.size() > len) {                                  // drop leading zeros beyond len
        size_t extra = out.size() - len;
        out = out.substr(extra);
    }
    return out;
}
string BigInt::to_hex() const {
    if (d_.empty()) return "0";
    string h = ec::to_hex(to_bytes(d_.size() * 4));
    size_t i = h.find_first_not_of('0');
    return h.substr(i);
}
int BigInt::cmp(const BigInt& o) const {
    if (d_.size() != o.d_.size()) return d_.size() < o.d_.size() ? -1 : 1;
    for (size_t i = d_.size(); i-- > 0;)
        if (d_[i] != o.d_[i]) return d_[i] < o.d_[i] ? -1 : 1;
    return 0;
}
BigInt BigInt::add(const BigInt& o) const {
    BigInt r;
    size_t n = std::max(d_.size(), o.d_.size());
    r.d_.resize(n + 1, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t s = carry + (i < d_.size() ? d_[i] : 0) + (i < o.d_.size() ? o.d_[i] : 0);
        r.d_[i] = (uint32_t)s;
        carry = s >> 32;
    }
    r.d_[n] = (uint32_t)carry;
    r.trim();
    return r;
}
BigInt BigInt::sub(const BigInt& o) const {
    BigInt r;
    r.d_.resize(d_.size(), 0);
    int64_t borrow = 0;
    for (size_t i = 0; i < d_.size(); i++) {
        int64_t s = (int64_t)d_[i] - borrow - (i < o.d_.size() ? (int64_t)o.d_[i] : 0);
        borrow = s < 0 ? 1 : 0;
        r.d_[i] = (uint32_t)(s + (borrow ? ((int64_t)1 << 32) : 0));
    }
    r.trim();
    return r;
}
BigInt BigInt::mul(const BigInt& o) const {
    BigInt r;
    if (d_.empty() || o.d_.empty()) return r;
    r.d_.assign(d_.size() + o.d_.size(), 0);
    for (size_t i = 0; i < d_.size(); i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < o.d_.size(); j++) {
            uint64_t t = (uint64_t)d_[i] * o.d_[j] + r.d_[i + j] + carry;
            r.d_[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        size_t k = i + o.d_.size();
        while (carry) {
            uint64_t t = (uint64_t)r.d_[k] + carry;
            r.d_[k++] = (uint32_t)t;
            carry = t >> 32;
        }
    }
    r.trim();
    return r;
}
size_t BigInt::bits() const {
    if (d_.empty()) return 0;
    size_t b = (d_.size() - 1) * 32;
    uint32_t top = d_.back();
    while (top) {
        b++;
        top >>= 1;
    }
    return b;
}

// Knuth, Algorithm D (32-bit limbs)
void divmod(const BigInt& a, const BigInt& b, BigInt* q, BigInt* r) {
    if (b.d_.empty()) throw Error("division by zero");
    if (a.cmp(b) < 0) {
        if (q) *q = BigInt();
        if (r) *r = a;
        return;
    }
    if (b.d_.size() == 1) {                                  // short division
        uint64_t rem = 0;
        BigInt qq;
        qq.d_.assign(a.d_.size(), 0);
        for (size_t i = a.d_.size(); i-- > 0;) {
            uint64_t cur = (rem << 32) | a.d_[i];
            qq.d_[i] = (uint32_t)(cur / b.d_[0]);
            rem = cur % b.d_[0];
        }
        qq.trim();
        if (q) *q = qq;
        if (r) *r = BigInt((uint32_t)rem);
        return;
    }
    int s = 0;
    uint32_t top = b.d_.back();
    while (!(top & 0x80000000u)) {
        top <<= 1;
        s++;
    }
    size_t n = b.d_.size(), m = a.d_.size() - n;
    vector<uint32_t> bn(n), an(a.d_.size() + 1);
    for (size_t i = n - 1; i > 0; i--) bn[i] = (b.d_[i] << s) | (s ? (uint32_t)((uint64_t)b.d_[i - 1] >> (32 - s)) : 0);
    bn[0] = b.d_[0] << s;
    an[a.d_.size()] = s ? (uint32_t)((uint64_t)a.d_.back() >> (32 - s)) : 0;
    for (size_t i = a.d_.size() - 1; i > 0; i--)
        an[i] = (a.d_[i] << s) | (s ? (uint32_t)((uint64_t)a.d_[i - 1] >> (32 - s)) : 0);
    an[0] = a.d_[0] << s;
    BigInt qq;
    qq.d_.assign(m + 1, 0);
    const uint64_t BASE = (uint64_t)1 << 32;
    for (size_t j = m + 1; j-- > 0;) {
        uint64_t num = ((uint64_t)an[j + n] << 32) | an[j + n - 1];
        uint64_t qhat = num / bn[n - 1], rhat = num % bn[n - 1];
        while (qhat >= BASE || qhat * bn[n - 2] > ((rhat << 32) | an[j + n - 2])) {
            qhat--;
            rhat += bn[n - 1];
            if (rhat >= BASE) break;
        }
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (size_t i = 0; i < n; i++) {
            uint64_t p = qhat * bn[i] + carry;
            carry = p >> 32;
            int64_t t = (int64_t)an[i + j] - borrow - (int64_t)(uint32_t)p;
            an[i + j] = (uint32_t)t;
            borrow = t < 0 ? 1 : 0;
        }
        int64_t t = (int64_t)an[j + n] - borrow - (int64_t)carry;
        an[j + n] = (uint32_t)t;
        if (t < 0) {                                         // took one too many: add back
            qhat--;
            uint64_t c = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t sum = (uint64_t)an[i + j] + bn[i] + c;
                an[i + j] = (uint32_t)sum;
                c = sum >> 32;
            }
            an[j + n] += (uint32_t)c;
        }
        qq.d_[j] = (uint32_t)qhat;
    }
    qq.trim();
    if (q) *q = qq;
    if (r) {
        BigInt rr;
        rr.d_.assign(n, 0);
        for (size_t i = 0; i < n; i++)
            rr.d_[i] = (an[i] >> s) | (s ? (uint32_t)((uint64_t)an[i + 1] << (32 - s)) : 0);
        rr.trim();
        *r = rr;
    }
}
BigInt BigInt::mod(const BigInt& m) const {
    BigInt r;
    divmod(*this, m, nullptr, &r);
    return r;
}
BigInt BigInt::modpow(BigInt base, const BigInt& exp, const BigInt& m) {
    BigInt result(1);
    base = base.mod(m);
    size_t nb = exp.bits();
    for (size_t i = nb; i-- > 0;) {
        result = result.mul(result).mod(m);
        if ((exp.d_[i / 32] >> (i % 32)) & 1) result = result.mul(base).mod(m);
    }
    return result.mod(m);
}

// ---------------------------------------------------------------- SRP
const BigInt& SRP_N() {
    static const BigInt n = BigInt::from_hex(
        "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4A099ED8193E0757767A13D"
        "D52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF6095179A163AB3661A05FBD5FAAAE82918A9962F0B93B8"
        "55F97993EC975EEAA80D740ADBF4FF747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481"
        "F1D2B9078717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6"
        "AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB694B5C803D89F7AE435DE236D525F5475"
        "9B65E372FCD68EF20FA7111F9E4AFF73");
    return n;
}
static const BigInt& SRP_G() {
    static const BigInt g(2);
    return g;
}
static const BigInt& SRP_K() {
    static const BigInt k = BigInt::from_bytes(sha256(SRP_N().to_bytes(SRP_LEN) + SRP_G().to_bytes(SRP_LEN)));
    return k;
}

BigInt srp_x(const string& salt, const string& pin) { return BigInt::from_bytes(sha256(salt + sha256("ecbot:" + pin))); }

SrpHello srp_server_hello(const string& pin) {
    SrpHello h;
    h.salt = random_bytes(16);
    h.v = BigInt::modpow(SRP_G(), srp_x(h.salt, pin), SRP_N());
    h.b = BigInt::from_bytes(random_bytes(32));
    h.B = SRP_K().mul(h.v).add(BigInt::modpow(SRP_G(), h.b, SRP_N())).mod(SRP_N());
    return h;
}

string srp_server_key(const BigInt& A, const BigInt& b, const BigInt& B, const BigInt& v) {
    BigInt u = BigInt::from_bytes(sha256(A.to_bytes(SRP_LEN) + B.to_bytes(SRP_LEN)));
    BigInt base = A.mul(BigInt::modpow(v, u, SRP_N())).mod(SRP_N());
    BigInt S = BigInt::modpow(base, b, SRP_N());
    return sha256(S.to_bytes(SRP_LEN));
}

std::pair<string, string> srp_proofs(const string& K, const BigInt& A, const BigInt& B) {
    string ab = A.to_bytes(SRP_LEN) + B.to_bytes(SRP_LEN);
    return {hmac_sha256(K, "c" + ab), hmac_sha256(K, "s" + ab)};
}

string be64(uint64_t n) {
    string out(8, '\0');
    for (int i = 0; i < 8; i++) out[i] = char((n >> (56 - 8 * i)) & 0xFF);
    return out;
}
static string be32(uint32_t n) {
    string out(4, '\0');
    for (int i = 0; i < 4; i++) out[i] = char((n >> (24 - 8 * i)) & 0xFF);
    return out;
}

string stream_xor(const string& kenc, const string& direction, uint64_t n, const string& data) {
    string head = direction + be64(n), out(data.size(), '\0');
    for (size_t i = 0; i < data.size(); i += 32) {
        string block = hmac_sha256(kenc, head + be32((uint32_t)(i / 32)));
        for (size_t j = 0; j < 32 && i + j < data.size(); j++) out[i + j] = char(data[i + j] ^ block[j]);
    }
    return out;
}

json seal(const Keys& k, const string& direction, uint64_t n, const json& obj) {
    string raw = obj.dump(-1, ' ', false, json::error_handler_t::replace);
    string c = stream_xor(k.enc, direction, n, raw);
    return {{"c", base64_encode(c)}, {"m", to_hex(hmac_sha256(k.mac, direction + be64(n) + c))}};
}

json unseal(const Keys& k, const string& direction, uint64_t n, const json& msg) {
    string c = base64_decode(msg.value("c", ""));
    string want = to_hex(hmac_sha256(k.mac, direction + be64(n) + c));
    if (!same_text(want, msg.value("m", ""))) throw Error("bad signature");
    return json::parse(stream_xor(k.enc, direction, n, c));
}

bool valid_pin(const string& pin) { return pin.size() == 6 && is_digits(pin); }

string new_pin() {
    string r = random_bytes(4);
    uint32_t v = ((uint32_t)(unsigned char)r[0] << 24) | ((uint32_t)(unsigned char)r[1] << 16) |
                 ((uint32_t)(unsigned char)r[2] << 8) | (uint32_t)(unsigned char)r[3];
    char buf[8];
    std::snprintf(buf, sizeof buf, "%06u", v % 1000000u);
    return buf;
}

}  // namespace ec
