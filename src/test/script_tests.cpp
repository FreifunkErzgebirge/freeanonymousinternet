// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "data/script_invalid.json.h"
#include "data/script_valid.json.h"

#include "core_io.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "util.h"

#if defined(HAVE_CONSENSUS_LIB)
#include "script/bitcoinconsensus.h"
#endif

#include <fstream>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

using namespace std;
using namespace json_spirit;
using namespace boost::algorithm;

// Uncomment if you want to output updated JSON tests.
// #define UPDATE_JSON_TESTS

static const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

unsigned int ParseScriptFlags(string strFlags);
string FormatScriptFlags(unsigned int flags);

Array
read_json(const std::string& jsondata)
{
    Value v;

    if (!read_string(jsondata, v) || v.type() != array_type)
    {
        BOOST_ERROR("Parse error.");
        return Array();
    }
    return v.get_array();
}

BOOST_AUTO_TEST_SUITE(script_tests)

CMutableTransaction BuildCreditingTransaction(const CScript& scriptPubKey)
{
    CMutableTransaction txCredit;
    txCredit.nVersion = 1;
    
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vout[0].scriptPubKey = scriptPubKey;
    txCredit.vout[0].nValue = 0;
    txCredit.vout[0].nLockTime = 0;
    return txCredit;
}

CMutableTransaction BuildSpendingTransaction(const CScript& scriptSig, const CMutableTransaction& txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hash = txCredit.GetHash();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = 0;
    txSpend.vout[0].nLockTime = 0;
    return txSpend;
}

void DoTest(const CScript& scriptPubKey, const CScript& scriptSig, int flags, bool expect, const std::string& message)
{
    CMutableTransaction tx = BuildSpendingTransaction(scriptSig, BuildCreditingTransaction(scriptPubKey));
    CMutableTransaction tx2 = tx;
#if defined(HAVE_CONSENSUS_LIB)
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx2;
#endif
}

void static NegateSignatureS(std::vector<unsigned char>& vchSig) {
    // Parse the signature.
    std::vector<unsigned char> r, s;
    r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
    s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
    unsigned char hashtype = vchSig.back();

    // Really ugly to implement mod-n negation here, but it would be feature creep to expose such functionality from libsecp256k1.
    static const unsigned char order[33] = {
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
    };
    while (s.size() < 33) {
        s.insert(s.begin(), 0x00);
    }
    int carry = 0;
    for (int p = 32; p >= 1; p--) {
        int n = (int)order[p] - s[p] - carry;
        s[p] = (n + 256) & 0xFF;
        carry = (n < 0);
    }
    assert(carry == 0);
    if (s.size() > 1 && s[0] == 0 && s[1] < 0x80) {
        s.erase(s.begin());
    }

    // Reconstruct the signature.
    vchSig.clear();
    vchSig.push_back(0x30);
    vchSig.push_back(4 + r.size() + s.size());
    vchSig.push_back(0x02);
    vchSig.push_back(r.size());
    vchSig.insert(vchSig.end(), r.begin(), r.end());
    vchSig.push_back(0x02);
    vchSig.push_back(s.size());
    vchSig.insert(vchSig.end(), s.begin(), s.end());
    vchSig.push_back(hashtype);
}

namespace
{
const unsigned char vchKey0[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
const unsigned char vchKey1[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0};
const unsigned char vchKey2[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0};

struct KeyData
{
    CKey key0, key0C, key1, key1C, key2, key2C;
    CPubKey pubkey0, pubkey0C, pubkey0H;
    CPubKey pubkey1, pubkey1C;
    CPubKey pubkey2, pubkey2C;

    KeyData()
    {

        key0.Set(vchKey0, vchKey0 + 32, false);
        key0C.Set(vchKey0, vchKey0 + 32, true);
        pubkey0 = key0.GetPubKey();
        pubkey0H = key0.GetPubKey();
        pubkey0C = key0C.GetPubKey();
        *const_cast<unsigned char*>(&pubkey0H[0]) = 0x06 | (pubkey0H[64] & 1);

        key1.Set(vchKey1, vchKey1 + 32, false);
        key1C.Set(vchKey1, vchKey1 + 32, true);
        pubkey1 = key1.GetPubKey();
        pubkey1C = key1C.GetPubKey();

        key2.Set(vchKey2, vchKey2 + 32, false);
        key2C.Set(vchKey2, vchKey2 + 32, true);
        pubkey2 = key2.GetPubKey();
        pubkey2C = key2C.GetPubKey();
    }
};


class TestBuilder
{
private:
    CScript scriptPubKey;
    CTransaction creditTx;
    CMutableTransaction spendTx;
    bool havePush;
    std::vector<unsigned char> push;
    std::string comment;
    int flags;

    void DoPush()
    {
        if (havePush) {
            spendTx.vin[0].scriptSig << push;
            havePush = false;
        }
    }

    void DoPush(const std::vector<unsigned char>& data)
    {
         DoPush();
         push = data;
         havePush = true;
    }

public:
    TestBuilder(const CScript& redeemScript, const std::string& comment_, int flags_, bool P2SH = false) : scriptPubKey(redeemScript), havePush(false), comment(comment_), flags(flags_)
    {
        if (P2SH) {
            creditTx = BuildCreditingTransaction(CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript)) << OP_EQUAL);
        } else {
            creditTx = BuildCreditingTransaction(redeemScript);
        }
        spendTx = BuildSpendingTransaction(CScript(), creditTx);
    }

    TestBuilder& Add(const CScript& script)
    {
        DoPush();
        spendTx.vin[0].scriptSig += script;
        return *this;
    }

    TestBuilder& Num(int num)
    {
        DoPush();
        spendTx.vin[0].scriptSig << num;
        return *this;
    }

    TestBuilder& Push(const std::string& hex)
    {
        DoPush(ParseHex(hex));
        return *this;
    }

    TestBuilder& PushSig(const CKey& key, int nHashType = SIGHASH_ALL, unsigned int lenR = 32, unsigned int lenS = 32)
    {
        uint256 hash = SignatureHash(scriptPubKey, spendTx, 0, nHashType);
        std::vector<unsigned char> vchSig, r, s;
        uint32_t iter = 0;
        do {
            key.Sign(hash, vchSig, iter++);
            if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33)) {
                NegateSignatureS(vchSig);
            }
            r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
            s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
        } while (lenR != r.size() || lenS != s.size());
        vchSig.push_back(static_cast<unsigned char>(nHashType));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder& Push(const CPubKey& pubkey)
    {
        DoPush(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
        return *this;
    }

    TestBuilder& PushRedeem()
    {
        DoPush(static_cast<std::vector<unsigned char> >(scriptPubKey));
        return *this;
    }

    TestBuilder& EditPush(unsigned int pos, const std::string& hexin, const std::string& hexout)
    {
        assert(havePush);
        std::vector<unsigned char> datain = ParseHex(hexin);
        std::vector<unsigned char> dataout = ParseHex(hexout);
        assert(pos + datain.size() <= push.size());
        BOOST_CHECK_MESSAGE(std::vector<unsigned char>(push.begin() + pos, push.begin() + pos + datain.size()) == datain, comment);
        push.erase(push.begin() + pos, push.begin() + pos + datain.size());
        push.insert(push.begin() + pos, dataout.begin(), dataout.end());
        return *this;
    }

    TestBuilder& DamagePush(unsigned int pos)
    {
        assert(havePush);
        assert(pos < push.size());
        push[pos] ^= 1;
        return *this;
    }

    TestBuilder& Test(bool expect)
    {
        TestBuilder copy = *this; // Make a copy so we can rollback the push.
        DoPush();
        DoTest(creditTx.vout[0].scriptPubKey, spendTx.vin[0].scriptSig, flags, expect, comment);
        *this = copy;
        return *this;
    }

    Array GetJSON()
    {
        DoPush();
        Array array;
        array.push_back(FormatScript(spendTx.vin[0].scriptSig));
        array.push_back(FormatScript(creditTx.vout[0].scriptPubKey));
        array.push_back(FormatScriptFlags(flags));
        array.push_back(comment);
        return array;
    }

    std::string GetComment()
    {
        return comment;
    }

    const CScript& GetScriptPubKey()
    {
        return creditTx.vout[0].scriptPubKey;
    }
};
}

BOOST_AUTO_TEST_CASE(script_build)
{
    const KeyData keys;

    std::vector<TestBuilder> good;
    std::vector<TestBuilder> bad;

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2PK", 0
                              ).PushSig(keys.key0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2PK, bad sig", 0
                             ).PushSig(keys.key0).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << keys.pubkey1C.GetID().ToByteVector() << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2PKH", 0
                              ).PushSig(keys.key1).Push(keys.pubkey1C));
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << keys.pubkey2C.GetID().ToByteVector() << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2PKH, bad pubkey", 0
                             ).PushSig(keys.key2).Push(keys.pubkey2C).DamagePush(5));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK anyonecanpay", 0
                              ).PushSig(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK anyonecanpay marked with normal hashtype", 0
                             ).PushSig(keys.key1, SIGHASH_ALL | SIGHASH_ANYONECANPAY).EditPush(70, "81", "01"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                               "P2SH(P2PK)", SCRIPT_VERIFY_P2SH, true
                              ).PushSig(keys.key0).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                              "P2SH(P2PK), bad redeemscript", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).PushRedeem().DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << keys.pubkey1.GetID().ToByteVector() << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2SH(P2PKH), bad sig but no VERIFY_P2SH", 0, true
                              ).PushSig(keys.key0).DamagePush(10).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << keys.pubkey1.GetID().ToByteVector() << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2SH(P2PKH), bad sig", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).DamagePush(10).PushRedeem());

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3", 0
                              ).Num(0).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3, 2 sigs", 0
                             ).Num(0).PushSig(keys.key0).PushSig(keys.key1).Num(0));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "P2SH(2-of-3)", SCRIPT_VERIFY_P2SH, true
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "P2SH(2-of-3), 1 sig", SCRIPT_VERIFY_P2SH, true
                             ).Num(0).PushSig(keys.key1).Num(0).PushRedeem());

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too much R padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too much S padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL).EditPush(1, "44", "45").EditPush(37, "20", "2100"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much S padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL).EditPush(1, "44", "45").EditPush(37, "20", "2100"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "P2PK with too little R padding but no DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too little R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with bad sig with too much R padding but no DERSIG", 0
                              ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000").DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with bad sig with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000").DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with too much R padding but no DERSIG", 0
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with too much R padding", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key2, SIGHASH_ALL, 31, 32).EditPush(1, "43021F", "44022000"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                               "BIP66 example 1, without DERSIG", 0
                              ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 1, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 2, without DERSIG", 0
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 2, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 3, without DERSIG", 0
                             ).Num(0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 3, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 4, without DERSIG", 0
                              ).Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 4, with DERSIG", SCRIPT_VERIFY_DERSIG
                              ).Num(0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 5, without DERSIG", 0
                             ).Num(1));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 5, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(1));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 6, without DERSIG", 0
                              ).Num(1));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 6, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(1));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                               "BIP66 example 7, without DERSIG", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 7, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 8, without DERSIG", 0
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 8, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 9, without DERSIG", 0
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 9, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 10, without DERSIG", 0
                              ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 10, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).Num(0).PushSig(keys.key2, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 11, without DERSIG", 0
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 11, with DERSIG", SCRIPT_VERIFY_DERSIG
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 12, without DERSIG", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 12, with DERSIG", SCRIPT_VERIFY_DERSIG
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL, 33, 32).EditPush(1, "45022100", "440220").Num(0));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with high S but no LOW_S", 0
                              ).PushSig(keys.key2, SIGHASH_ALL, 32, 33));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2PK with high S", SCRIPT_VERIFY_LOW_S
                             ).PushSig(keys.key2, SIGHASH_ALL, 32, 33));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                               "P2PK with hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                              "P2PK with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey but no STRICTENC", 0
                             ).PushSig(keys.key0, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, SIGHASH_ALL).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, SIGHASH_ALL).DamagePush(10));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey and no STRICTENC", 0
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                              ).Num(0).PushSig(keys.key1, SIGHASH_ALL));
    bad.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2 << OP_CHECKMULTISIG,
                              "1-of-2 with the first 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).Num(0).PushSig(keys.key1, SIGHASH_ALL));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK with undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK with undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid sig and undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3 with nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3 with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                               "3-of-3 NOT with invalid sig and nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                              "3-of-3 NOT with invalid sig with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed using OP_DUP but no SIGPUSHONLY", 0
                              ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                              "2-of-2 with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY
                             ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig but no SIGPUSHONLY", 0
                             ).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig", SCRIPT_VERIFY_SIGPUSHONLY
                             ).PushSig(keys.key2).PushRedeem());
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key1));


    std::set<std::string> tests_good;
    std::set<std::string> tests_bad;

    {
        Array json_good = read_json(std::string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));
        Array json_bad = read_json(std::string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

        BOOST_FOREACH(Value& tv, json_good) {
            tests_good.insert(write_string(Value(tv.get_array()), true));
        }
        BOOST_FOREACH(Value& tv, json_bad) {
            tests_bad.insert(write_string(Value(tv.get_array()), true));
        }
    }

    std::string strGood;
    std::string strBad;

    BOOST_FOREACH(TestBuilder& test, good) {
        test.Test(true);
        std::string str = write_string(Value(test.GetJSON()), true);
#ifndef UPDATE_JSON_TESTS
        if (tests_good.count(str) == 0) {
        }
#endif
        strGood += str + ",\n";
    }
    BOOST_FOREACH(TestBuilder& test, bad) {
        test.Test(false);
        std::string str = write_string(Value(test.GetJSON()), true);
#ifndef UPDATE_JSON_TESTS
        if (tests_bad.count(str) == 0) {
        }
#endif
        strBad += str + ",\n";
    }

#ifdef UPDATE_JSON_TESTS
    FILE* valid = fopen("script_valid.json.gen", "w");
    fputs(strGood.c_str(), valid);
    fclose(valid);
    FILE* invalid = fopen("script_invalid.json.gen", "w");
    fputs(strBad.c_str(), invalid);
    fclose(invalid);
#endif
}

BOOST_AUTO_TEST_CASE(script_valid)
{
    // Read tests from test/data/script_valid.json
    // Format is an array of arrays
    // Inner arrays are [ "scriptSig", "scriptPubKey", "flags" ]
    // ... where scriptSig and scriptPubKey are stringified
    // scripts.
    Array tests = read_json(std::string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));

    BOOST_FOREACH(Value& tv, tests)
    {
        Array test = tv.get_array();
        string strTest = write_string(tv, false);
        if (test.size() < 3) // Allow size > 3; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                BOOST_ERROR("Bad test: " << strTest);
            }
            continue;
        }
        string scriptSigString = test[0].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        string scriptPubKeyString = test[1].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[2].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, true, strTest);
    }
}

BOOST_AUTO_TEST_CASE(script_invalid)
{
    // Scripts that should evaluate as invalid
    Array tests = read_json(std::string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

    BOOST_FOREACH(Value& tv, tests)
    {
        Array test = tv.get_array();
        string strTest = write_string(tv, false);
        if (test.size() < 3) // Allow size > 3; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                BOOST_ERROR("Bad test: " << strTest);
            }
            continue;
        }
        string scriptSigString = test[0].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        string scriptPubKeyString = test[1].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[2].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, false, strTest);
    }
}

BOOST_AUTO_TEST_CASE(script_PushData)
{
    // Check that PUSHDATA1, PUSHDATA2, and PUSHDATA4 create the same value on
    // the stack as the 1-75 opcodes do.
    static const unsigned char direct[] = { 1, 0x5a };
    static const unsigned char pushdata1[] = { OP_PUSHDATA1, 1, 0x5a };
    static const unsigned char pushdata2[] = { OP_PUSHDATA2, 1, 0, 0x5a };
    static const unsigned char pushdata4[] = { OP_PUSHDATA4, 1, 0, 0, 0, 0x5a };

    ScriptError err;
    vector<vector<unsigned char> > directStack;
    BOOST_CHECK(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), true, BaseSignatureChecker(), &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata1Stack;
    BOOST_CHECK(EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), true, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata1Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata2Stack;
    BOOST_CHECK(EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), true, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata2Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    vector<vector<unsigned char> > pushdata4Stack;
    BOOST_CHECK(EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), true, BaseSignatureChecker(), &err));
    BOOST_CHECK(pushdata4Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
}

CScript
sign_multisig(CScript scriptPubKey, std::vector<CKey> keys, CTransaction transaction)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, 0, SIGHASH_ALL);

    CScript result;
    //
    // NOTE: CHECKMULTISIG has an unfortunate bug; it requires
    // one extra item on the stack, before the signatures.
    // Putting OP_0 on the stack is the workaround;
    // fixing the bug would mean splitting the block chain (old
    // clients would not accept new CHECKMULTISIG transactions,
    // and vice-versa)
    //
    result << OP_0;
    BOOST_FOREACH(const CKey &key, keys)
    {
        vector<unsigned char> vchSig;
        BOOST_CHECK(key.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}
CScript
sign_multisig(CScript scriptPubKey, const CKey &key, CTransaction transaction)
{
    std::vector<CKey> keys;
    keys.push_back(key);
    return sign_multisig(scriptPubKey, keys, transaction);
}

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG12)
{
}

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG23)
{
}    

BOOST_AUTO_TEST_CASE(script_combineSigs)
{
}

BOOST_AUTO_TEST_CASE(script_standard_push)
{
}

BOOST_AUTO_TEST_CASE(script_IsPushOnly_on_invalid_scripts)
{
    // IsPushOnly returns false when given a script containing only pushes that
    // are invalid due to truncation. IsPushOnly() is consensus critical
    // because P2SH evaluation uses it, although this specific behavior should
    // not be consensus critical as the P2SH evaluation would fail first due to
    // the invalid push. Still, it doesn't hurt to test it explicitly.
    static const unsigned char direct[] = { 1 };
    BOOST_CHECK(!CScript(direct, direct+sizeof(direct)).IsPushOnly());
}

BOOST_AUTO_TEST_SUITE_END()
