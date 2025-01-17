/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "B3Compilation.h"
#include "InitializeThreading.h"
#include "JSCJSValueInlines.h"
#include "JSString.h"
#include "LLIntThunks.h"
#include "ProtoCallFrame.h"
#include "VM.h"
#include "WasmMemory.h"
#include "WasmPlan.h"

#include <wtf/DataLog.h>
#include <wtf/LEBDecoder.h>

class CommandLine {
public:
    CommandLine(int argc, char** argv)
    {
        parseArguments(argc, argv);
    }

    Vector<String> m_arguments;
    bool m_runLEBTests { false };
    bool m_runWasmTests { false };

    void parseArguments(int, char**);
};

static NO_RETURN void printUsageStatement(bool help = false)
{
    fprintf(stderr, "Usage: testWasm [options]\n");
    fprintf(stderr, "  -h|--help  Prints this help message\n");
    fprintf(stderr, "  -l|--leb   Runs the LEB decoder tests\n");
    fprintf(stderr, "  -w|--web   Run the Wasm tests\n");
    fprintf(stderr, "\n");

    exit(help ? EXIT_SUCCESS : EXIT_FAILURE);
}

void CommandLine::parseArguments(int argc, char** argv)
{
    int i = 1;

    for (; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
            printUsageStatement(true);

        if (!strcmp(arg, "-l") || !strcmp(arg, "--leb"))
            m_runLEBTests = true;

        if (!strcmp(arg, "-w") || !strcmp(arg, "--web"))
            m_runWasmTests = true;
    }

    for (; i < argc; ++i)
        m_arguments.append(argv[i]);

}

StaticLock crashLock;

#define CHECK_EQ(x, y) do { \
        auto __x = (x); \
        auto __y = (y); \
        if (__x == __y) \
        break; \
        crashLock.lock(); \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, toCString(#x " == " #y, " (" #x " == ", __x, ", " #y " == ", __y, ")").data()); \
        CRASH(); \
    } while (false)

#define FOR_EACH_UNSIGNED_LEB_TEST(macro) \
    /* Simple tests that use all the bits in the array */ \
    macro(({ 0x07 }), 0, true, 0x7lu, 1lu) \
    macro(({ 0x77 }), 0, true, 0x77lu, 1lu) \
    macro(({ 0x80, 0x07 }), 0, true, 0x380lu, 2lu) \
    macro(({ 0x89, 0x12 }), 0, true, 0x909lu, 2lu) \
    macro(({ 0xf3, 0x85, 0x02 }), 0, true, 0x82f3lu, 3lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74 }), 0, true, 0xe9fc2f3lu, 4lu) \
    macro(({ 0xf3, 0x85, 0xff, 0xf4, 0x7f }), 0, true, 0xfe9fc2f3lu, 5lu) \
    /* Test with extra trailing numbers */ \
    macro(({ 0x07, 0x80 }), 0, true, 0x7lu, 1lu) \
    macro(({ 0x07, 0x75 }), 0, true, 0x7lu, 1lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74, 0x43 }), 0, true, 0xe9fc2f3lu, 4lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74, 0x80 }), 0, true, 0xe9fc2f3lu, 4lu) \
    /* Test with preceeding numbers */ \
    macro(({ 0xf3, 0x07 }), 1, true, 0x7lu, 2lu) \
    macro(({ 0x03, 0x07 }), 1, true, 0x7lu, 2lu) \
    macro(({ 0xf2, 0x53, 0x43, 0x67, 0x79, 0x77 }), 5, true, 0x77lu, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf7, 0x84, 0x77 }), 5, true, 0x77lu, 6ul) \
    macro(({ 0xf2, 0x53, 0x43, 0xf3, 0x85, 0x02 }), 3, true, 0x82f3lu, 6lu) \
    /* Test in the middle */ \
    macro(({ 0xf3, 0x07, 0x89 }), 1, true, 0x7lu, 2lu) \
    macro(({ 0x03, 0x07, 0x23 }), 1, true, 0x7lu, 2lu) \
    macro(({ 0xf2, 0x53, 0x43, 0x67, 0x79, 0x77, 0x43 }), 5, true, 0x77lu, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf7, 0x84, 0x77, 0xf9 }), 5, true, 0x77lu, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf3, 0x85, 0x02, 0xa4 }), 3, true, 0x82f3lu, 6lu) \
    /* Test decode too long */ \
    macro(({ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}), 0, false, 0x0lu, 0lu) \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 1, false, 0x0lu, 0lu) \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 0, false, 0x0lu, 0lu) \
    /* Test decode off end of array */ \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 2, false, 0x0lu, 0lu) \


#define TEST_UNSIGNED_LEB_DECODE(init, startOffset, expectedStatus, expectedResult, expectedOffset) \
    { \
        Vector<uint8_t> vector = Vector<uint8_t> init; \
        size_t offset = startOffset; \
        uint32_t result; \
        bool status = WTF::LEBDecoder::decodeUInt32(vector.data(), vector.size(), offset, result); \
        CHECK_EQ(status, expectedStatus); \
        if (expectedStatus) { \
            CHECK_EQ(result, expectedResult); \
            CHECK_EQ(offset, expectedOffset); \
        } \
    };


#define FOR_EACH_SIGNED_LEB_TEST(macro) \
    /* Simple tests that use all the bits in the array */ \
    macro(({ 0x07 }), 0, true, 0x7, 1lu) \
    macro(({ 0x77 }), 0, true, -0x9, 1lu) \
    macro(({ 0x80, 0x07 }), 0, true, 0x380, 2lu) \
    macro(({ 0x89, 0x12 }), 0, true, 0x909, 2lu) \
    macro(({ 0xf3, 0x85, 0x02 }), 0, true, 0x82f3, 3lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74 }), 0, true, 0xfe9fc2f3, 4lu) \
    macro(({ 0xf3, 0x85, 0xff, 0xf4, 0x7f }), 0, true, 0xfe9fc2f3, 5lu) \
    /* Test with extra trailing numbers */ \
    macro(({ 0x07, 0x80 }), 0, true, 0x7, 1lu) \
    macro(({ 0x07, 0x75 }), 0, true, 0x7, 1lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74, 0x43 }), 0, true, 0xfe9fc2f3, 4lu) \
    macro(({ 0xf3, 0x85, 0xff, 0x74, 0x80 }), 0, true, 0xfe9fc2f3, 4lu) \
    /* Test with preceeding numbers */ \
    macro(({ 0xf3, 0x07 }), 1, true, 0x7, 2lu) \
    macro(({ 0x03, 0x07 }), 1, true, 0x7, 2lu) \
    macro(({ 0xf2, 0x53, 0x43, 0x67, 0x79, 0x77 }), 5, true, -0x9, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf7, 0x84, 0x77 }), 5, true, -0x9, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf3, 0x85, 0x02 }), 3, true, 0x82f3, 6lu) \
    /* Test in the middle */ \
    macro(({ 0xf3, 0x07, 0x89 }), 1, true, 0x7, 2lu) \
    macro(({ 0x03, 0x07, 0x23 }), 1, true, 0x7, 2lu) \
    macro(({ 0xf2, 0x53, 0x43, 0x67, 0x79, 0x77, 0x43 }), 5, true, -0x9, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf7, 0x84, 0x77, 0xf9 }), 5, true, -0x9, 6lu) \
    macro(({ 0xf2, 0x53, 0x43, 0xf3, 0x85, 0x02, 0xa4 }), 3, true, 0x82f3, 6lu) \
    /* Test decode too long */ \
    macro(({ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}), 0, false, 0x0, 0lu) \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 1, false, 0x0, 0lu) \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 0, false, 0x0, 0lu) \
    /* Test decode off end of array */ \
    macro(({ 0x80, 0x80, 0xab, 0x8a, 0x9a, 0xa3, 0xff}), 2, false, 0x0, 0lu) \


#define TEST_SIGNED_LEB_DECODE(init, startOffset, expectedStatus, expectedResult, expectedOffset) \
    { \
        Vector<uint8_t> vector = Vector<uint8_t> init; \
        size_t offset = startOffset; \
        int32_t result; \
        bool status = WTF::LEBDecoder::decodeInt32(vector.data(), vector.size(), offset, result); \
        CHECK_EQ(status, expectedStatus); \
        if (expectedStatus) { \
            int32_t expected = expectedResult; \
            CHECK_EQ(result, expected); \
            CHECK_EQ(offset, expectedOffset); \
        } \
    };


static void runLEBTests()
{
    FOR_EACH_UNSIGNED_LEB_TEST(TEST_UNSIGNED_LEB_DECODE)
    FOR_EACH_SIGNED_LEB_TEST(TEST_SIGNED_LEB_DECODE)
}

#if ENABLE(WEBASSEMBLY)

static JSC::VM* vm;

using namespace JSC;
using namespace Wasm;
using namespace B3;

template<typename T>
T invoke(MacroAssemblerCodePtr ptr, std::initializer_list<JSValue> args)
{
    JSValue firstArgument;
    // Since vmEntryToJavaScript expects a this value we claim there is one... there isn't.
    int argCount = 1;
    JSValue* remainingArguments = nullptr;
    if (args.size()) {
        remainingArguments = const_cast<JSValue*>(args.begin());
        firstArgument = *remainingArguments;
        remainingArguments++;
        argCount = args.size();
    }

    ProtoCallFrame protoCallFrame;
    protoCallFrame.init(nullptr, nullptr, firstArgument, argCount, remainingArguments);

    // This won't work for floating point values but we don't have those yet.
    return static_cast<T>(vmEntryToWasm(ptr.executableAddress(), vm, &protoCallFrame));
}

template<typename T>
T invoke(const Compilation& code, std::initializer_list<JSValue> args)
{
    return invoke<T>(code.code(), args);
}

inline JSValue box(uint64_t value)
{
    return JSValue::decode(value);
}

// For now we inline the test files.
static void runWasmTests()
{
    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func $sum12 (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (param i32) (result i32) (return (i32.add (get_local 0) (i32.add (get_local 1) (i32.add (get_local 2) (i32.add (get_local 3) (i32.add (get_local 4) (i32.add (get_local 5) (i32.add (get_local 6) (i32.add (get_local 7) (i32.add (get_local 8) (i32.add (get_local 9) (i32.add (get_local 10) (get_local 11))))))))))))))
        //     (func (export "mult12") (param i32) (result i32) (return (call $sum12 (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0) (get_local 0))))
        //     )

        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x96, 0x80, 0x80, 0x80, 0x00, 0x02, 0x40,
            0x0c, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x83, 0x80, 0x80, 0x80, 0x00, 0x02, 0x00, 0x01, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x00, 0x01, 0x07, 0x8a, 0x80, 0x80, 0x80, 0x00, 0x01, 0x06, 0x6d, 0x75,
            0x6c, 0x74, 0x31, 0x32, 0x00, 0x01, 0x0a, 0xce, 0x80, 0x80, 0x80, 0x00, 0x02, 0xa6, 0x80, 0x80,
            0x80, 0x00, 0x00, 0x14, 0x00, 0x14, 0x01, 0x14, 0x02, 0x14, 0x03, 0x14, 0x04, 0x14, 0x05, 0x14,
            0x06, 0x14, 0x07, 0x14, 0x08, 0x14, 0x09, 0x14, 0x0a, 0x14, 0x0b, 0x40, 0x40, 0x40, 0x40, 0x40,
            0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x09, 0x0f, 0x9d, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00,
            0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00,
            0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x16, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 2 || !plan.result[0] || !plan.result[1]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(100) }), 1200);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(1) }), 12);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2), box(3), box(4), box(5), box(6), box(7), box(8), box(9), box(10), box(11), box(12) }), 78);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2), box(3), box(4), box(5), box(6), box(7), box(8), box(9), box(10), box(11), box(100) }), 166);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func $fac (export "fac") (param i64) (result i64)
        //      (if (i64.eqz (get_local 0))
        //       (return (i64.const 1))
        //       )
        //      (return (i64.mul (get_local 0) (call $fac (i64.sub (get_local 0) (i64.const 1)))))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x02, 0x01, 0x02, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x00, 0x01, 0x07, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x03, 0x66, 0x61, 0x63,
            0x00, 0x00, 0x0a, 0x9e, 0x80, 0x80, 0x80, 0x00, 0x01, 0x98, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14,
            0x00, 0x11, 0x00, 0x68, 0x03, 0x00, 0x11, 0x01, 0x09, 0x0f, 0x14, 0x00, 0x14, 0x00, 0x11, 0x01,
            0x5c, 0x16, 0x00, 0x5d, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2) }), 2);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(4) }), 24);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "double") (param i64) (result i64) (return (call 1 (get_local 0) (get_local 0))))
        //     (func $sum (param i64) (param i64) (result i64) (return (i64.add (get_local 0) (get_local 1))))
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x8c, 0x80, 0x80, 0x80, 0x00, 0x02, 0x40,
            0x01, 0x02, 0x01, 0x02, 0x40, 0x02, 0x02, 0x02, 0x01, 0x02, 0x03, 0x83, 0x80, 0x80, 0x80, 0x00,
            0x02, 0x00, 0x01, 0x05, 0x83, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x01, 0x07, 0x8a, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x06, 0x64, 0x6f, 0x75, 0x62, 0x6c, 0x65, 0x00, 0x00, 0x0a, 0x9c, 0x80, 0x80,
            0x80, 0x00, 0x02, 0x89, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0x16, 0x01, 0x09,
            0x0f, 0x88, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00, 0x14, 0x01, 0x5b, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 2 || !plan.result[0] || !plan.result[1]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(0), box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(100), box(0) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(1), box(15) }), 16);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100) }), 200);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 2);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func $id (param $value i32) (result i32) (return (get_local $value)))
        //     (func (export "id-call") (param $value i32) (result i32) (return (call $id (get_local $value))))
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x83, 0x80, 0x80, 0x80, 0x00, 0x02, 0x00, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8b, 0x80, 0x80, 0x80, 0x00, 0x01, 0x07, 0x69, 0x64,
            0x2d, 0x63, 0x61, 0x6c, 0x6c, 0x00, 0x01, 0x0a, 0x97, 0x80, 0x80, 0x80, 0x00, 0x02, 0x85, 0x80,
            0x80, 0x80, 0x00, 0x00, 0x14, 0x00, 0x09, 0x0f, 0x87, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00,
            0x16, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 2 || !plan.result[0] || !plan.result[1]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(100) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[1]->jsEntryPoint, { box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 1);
    }

    {
        // Generated from:
        // (module
        //  (memory 1)
        //  (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //   (i64.store (get_local $ptr) (get_local $i))
        //   (return (i64.load (get_local $ptr)))
        //   )
        //  )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x02, 0x01, 0x01, 0x02, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x34, 0x03, 0x00, 0x14,
            0x01, 0x2b, 0x03, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
    }

    {
        // Generated from:
        // (module
        //  (memory 1)
        //  (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //   (i32.store (get_local $ptr) (get_local $i))
        //   (return (i32.load (get_local $ptr)))
        //   )
        //  )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x33, 0x02, 0x00, 0x14,
            0x01, 0x2a, 0x02, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "write_array") (param $x i32) (param $p i32) (param $length i32) (local $i i32)
        //      (set_local $i (i32.const 0))
        //      (block
        //       (loop
        //        (br_if 1 (i32.ge_u (get_local $i) (get_local $length)))
        //        (i32.store (i32.add (get_local $p) (i32.mul (get_local $i) (i32.const 4))) (get_local $x))
        //        (set_local $i (i32.add (i32.const 1) (get_local $i)))
        //        (br 0)
        //        )
        //       )
        //      (return)
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x03, 0x01, 0x01, 0x01, 0x00, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x77, 0x72,
            0x69, 0x74, 0x65, 0x5f, 0x61, 0x72, 0x72, 0x61, 0x79, 0x00, 0x00, 0x0a, 0xb2, 0x80, 0x80, 0x80,
            0x00, 0x01, 0xac, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x10, 0x00, 0x15, 0x03, 0x01, 0x00,
            0x02, 0x00, 0x14, 0x03, 0x14, 0x02, 0x56, 0x07, 0x01, 0x14, 0x01, 0x14, 0x03, 0x10, 0x04, 0x42,
            0x40, 0x14, 0x00, 0x33, 0x02, 0x00, 0x10, 0x01, 0x14, 0x03, 0x40, 0x15, 0x03, 0x06, 0x00, 0x0f,
            0x0f, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        unsigned length = 5;
        unsigned offset = sizeof(uint32_t);
        uint32_t* memory = static_cast<uint32_t*>(plan.memory->memory());
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(100), box(offset), box(length) });
        offset /= sizeof(uint32_t);
        CHECK_EQ(memory[offset - 1], 0u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 100u);

        length = 10;
        offset = 5 * sizeof(uint32_t);
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(5), box(offset), box(length) });
        offset /= sizeof(uint32_t);
        CHECK_EQ(memory[offset - 1], 100u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 5u);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "write_array") (param $x i32) (param $p i32) (param $length i32) (local $i i32)
        //      (set_local $i (i32.const 0))
        //      (block
        //       (loop
        //        (br_if 1 (i32.ge_u (get_local $i) (get_local $length)))
        //        (i32.store8 (i32.add (get_local $p) (get_local $i)) (get_local $x))
        //        (set_local $i (i32.add (i32.const 1) (get_local $i)))
        //        (br 0)
        //        )
        //       )
        //      (return)
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x03, 0x01, 0x01, 0x01, 0x00, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x77, 0x72,
            0x69, 0x74, 0x65, 0x5f, 0x61, 0x72, 0x72, 0x61, 0x79, 0x00, 0x00, 0x0a, 0xaf, 0x80, 0x80, 0x80,
            0x00, 0x01, 0xa9, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x10, 0x00, 0x15, 0x03, 0x01, 0x00,
            0x02, 0x00, 0x14, 0x03, 0x14, 0x02, 0x56, 0x07, 0x01, 0x14, 0x01, 0x14, 0x03, 0x40, 0x14, 0x00,
            0x2e, 0x00, 0x00, 0x10, 0x01, 0x14, 0x03, 0x40, 0x15, 0x03, 0x06, 0x00, 0x0f, 0x0f, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        unsigned length = 5;
        unsigned offset = 1;
        uint8_t* memory = static_cast<uint8_t*>(plan.memory->memory());
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(100), box(offset), box(length) });
        CHECK_EQ(memory[offset - 1], 0u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 100u);

        length = 10;
        offset = 5;
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(5), box(offset), box(length) });
        CHECK_EQ(memory[offset - 1], 100u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 5u);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //      (i32.store8 (get_local $ptr) (get_local $i))
        //      (return (i32.load8_s (get_local $ptr)))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x2e, 0x00, 0x00, 0x14,
            0x01, 0x20, 0x00, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "i32_load8_s") (param $i i32) (result i32)
        //      (i32.store8 (i32.const 8) (get_local $i))
        //      (return (i32.load8_s (i32.const 8)))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33, 0x32,
            0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80, 0x00,
            0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10, 0x08, 0x14, 0x00, 0x2e, 0x00, 0x00, 0x10, 0x08,
            0x20, 0x00, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 1);
    }

    {
        // Generated from:
        // (module
        //  (memory 1)
        //  (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //   (i32.store (get_local $ptr) (get_local $i))
        //   (return (i32.load (get_local $ptr)))
        //   )
        //  )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x02, 0x01, 0x01, 0x02, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x34, 0x03, 0x00, 0x14,
            0x01, 0x2b, 0x03, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(-12), box(plan.memory->size() - sizeof(uint64_t)) }), -12);
    }

    {
        // Generated from:
        // (module
        //  (memory 1)
        //  (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //   (i32.store (get_local $ptr) (get_local $i))
        //   (return (i32.load (get_local $ptr)))
        //   )
        //  )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x33, 0x02, 0x00, 0x14,
            0x01, 0x2a, 0x02, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "write_array") (param $x i32) (param $p i32) (param $length i32) (local $i i32)
        //      (set_local $i (i32.const 0))
        //      (block
        //       (loop
        //        (br_if 1 (i32.ge_u (get_local $i) (get_local $length)))
        //        (i32.store (i32.add (get_local $p) (i32.mul (get_local $i) (i32.const 4))) (get_local $x))
        //        (set_local $i (i32.add (i32.const 1) (get_local $i)))
        //        (br 0)
        //        )
        //       )
        //      (return)
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x03, 0x01, 0x01, 0x01, 0x00, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x77, 0x72,
            0x69, 0x74, 0x65, 0x5f, 0x61, 0x72, 0x72, 0x61, 0x79, 0x00, 0x00, 0x0a, 0xb2, 0x80, 0x80, 0x80,
            0x00, 0x01, 0xac, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x10, 0x00, 0x15, 0x03, 0x01, 0x00,
            0x02, 0x00, 0x14, 0x03, 0x14, 0x02, 0x56, 0x07, 0x01, 0x14, 0x01, 0x14, 0x03, 0x10, 0x04, 0x42,
            0x40, 0x14, 0x00, 0x33, 0x02, 0x00, 0x10, 0x01, 0x14, 0x03, 0x40, 0x15, 0x03, 0x06, 0x00, 0x0f,
            0x0f, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        unsigned length = 5;
        unsigned offset = sizeof(uint32_t);
        uint32_t* memory = static_cast<uint32_t*>(plan.memory->memory());
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(100), box(offset), box(length) });
        offset /= sizeof(uint32_t);
        CHECK_EQ(memory[offset - 1], 0u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 100u);

        length = 10;
        offset = 5 * sizeof(uint32_t);
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(5), box(offset), box(length) });
        offset /= sizeof(uint32_t);
        CHECK_EQ(memory[offset - 1], 100u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 5u);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "write_array") (param $x i32) (param $p i32) (param $length i32) (local $i i32)
        //      (set_local $i (i32.const 0))
        //      (block
        //       (loop
        //        (br_if 1 (i32.ge_u (get_local $i) (get_local $length)))
        //        (i32.store8 (i32.add (get_local $p) (get_local $i)) (get_local $x))
        //        (set_local $i (i32.add (i32.const 1) (get_local $i)))
        //        (br 0)
        //        )
        //       )
        //      (return)
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x03, 0x01, 0x01, 0x01, 0x00, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x77, 0x72,
            0x69, 0x74, 0x65, 0x5f, 0x61, 0x72, 0x72, 0x61, 0x79, 0x00, 0x00, 0x0a, 0xaf, 0x80, 0x80, 0x80,
            0x00, 0x01, 0xa9, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x10, 0x00, 0x15, 0x03, 0x01, 0x00,
            0x02, 0x00, 0x14, 0x03, 0x14, 0x02, 0x56, 0x07, 0x01, 0x14, 0x01, 0x14, 0x03, 0x40, 0x14, 0x00,
            0x2e, 0x00, 0x00, 0x10, 0x01, 0x14, 0x03, 0x40, 0x15, 0x03, 0x06, 0x00, 0x0f, 0x0f, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        unsigned length = 5;
        unsigned offset = 1;
        uint8_t* memory = static_cast<uint8_t*>(plan.memory->memory());
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(100), box(offset), box(length) });
        CHECK_EQ(memory[offset - 1], 0u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 100u);

        length = 10;
        offset = 5;
        invoke<void>(*plan.result[0]->jsEntryPoint, { box(5), box(offset), box(length) });
        CHECK_EQ(memory[offset - 1], 100u);
        CHECK_EQ(memory[offset + length], 0u);
        for (unsigned i = 0; i < length; ++i)
            CHECK_EQ(memory[i + offset], 5u);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "i32_load8_s") (param $i i32) (param $ptr i32) (result i32)
        //      (i32.store8 (get_local $ptr) (get_local $i))
        //      (return (i32.load8_s (get_local $ptr)))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33,
            0x32, 0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x01, 0x14, 0x00, 0x2e, 0x00, 0x00, 0x14,
            0x01, 0x20, 0x00, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }
        ASSERT(plan.memory->size());

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(10) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(2) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(100) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (memory 1)
        //     (func (export "i32_load8_s") (param $i i32) (result i32)
        //      (i32.store8 (i32.const 8) (get_local $i))
        //      (return (i32.load8_s (i32.const 8)))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x05, 0x83, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x01, 0x01, 0x07, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x01, 0x0b, 0x69, 0x33, 0x32,
            0x5f, 0x6c, 0x6f, 0x61, 0x64, 0x38, 0x5f, 0x73, 0x00, 0x00, 0x0a, 0x95, 0x80, 0x80, 0x80, 0x00,
            0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10, 0x08, 0x14, 0x00, 0x2e, 0x00, 0x00, 0x10, 0x08,
            0x20, 0x00, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100) }), 100);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (func "dumb-eq" (param $x i32) (param $y i32) (result i32)
        //      (if (i32.eq (get_local $x) (get_local $y))
        //       (then (br 0))
        //       (else (return (i32.const 1))))
        //      (return (i32.const 0))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8b, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x07, 0x64, 0x75, 0x6d, 0x62, 0x2d, 0x65, 0x71, 0x00, 0x00, 0x0a, 0x99,
            0x80, 0x80, 0x80, 0x00, 0x01, 0x93, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00, 0x14, 0x01, 0x4d,
            0x03, 0x00, 0x06, 0x00, 0x04, 0x10, 0x01, 0x09, 0x0f, 0x10, 0x00, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(0) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(2) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(6) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(6) }), 1);
    }

    {
        // Generated from:
        //    (module
        //     (func (export "dumb-less-than") (param $x i32) (param $y i32) (result i32)
        //      (loop
        //       (if (i32.eq (get_local $x) (get_local $y))
        //        (then (return (i32.const 0)))
        //        (else (if (i32.eq (get_local $x) (i32.const 0))
        //               (return (i32.const 1)))))
        //       (set_local $x (i32.sub (get_local $x) (i32.const 1)))
        //       (br 0)
        //       )
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x92, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x0e, 0x64, 0x75, 0x6d, 0x62, 0x2d, 0x6c, 0x65, 0x73, 0x73, 0x2d, 0x74,
            0x68, 0x61, 0x6e, 0x00, 0x00, 0x0a, 0xac, 0x80, 0x80, 0x80, 0x00, 0x01, 0xa6, 0x80, 0x80, 0x80,
            0x00, 0x00, 0x02, 0x00, 0x14, 0x00, 0x14, 0x01, 0x4d, 0x03, 0x00, 0x10, 0x00, 0x09, 0x04, 0x14,
            0x00, 0x10, 0x00, 0x4d, 0x03, 0x00, 0x10, 0x01, 0x09, 0x0f, 0x0f, 0x14, 0x00, 0x10, 0x01, 0x41,
            0x15, 0x00, 0x06, 0x00, 0x0f, 0x00, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(2) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(6) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(6) }), 0);
    }


    {
        // Generated from: (module (func (export "return-i32") (result i32) (return (i32.const 5))) )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x85, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x00, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8e, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x0a, 0x72, 0x65, 0x74, 0x75, 0x72, 0x6e, 0x2d, 0x69, 0x33, 0x32, 0x00, 0x00, 0x0a,
            0x8b, 0x80, 0x80, 0x80, 0x00, 0x01, 0x85, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10, 0x05, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { }), 5);
    }


    {
        // Generated from: (module (func (export "return-i32") (result i32) (return (i32.add (i32.const 5) (i32.const 6)))) )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x85, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x00, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8e, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x0a, 0x72, 0x65, 0x74, 0x75, 0x72, 0x6e, 0x2d, 0x69, 0x33, 0x32, 0x00, 0x00, 0x0a,
            0x8e, 0x80, 0x80, 0x80, 0x00, 0x01, 0x88, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10, 0x05, 0x10, 0x06,
            0x40, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { }), 11);
    }

    {
        // Generated from: (module (func (export "return-i32") (result i32) (return (i32.add (i32.add (i32.const 5) (i32.const 3)) (i32.const 3)))) )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x85, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x00, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8e, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x0a, 0x72, 0x65, 0x74, 0x75, 0x72, 0x6e, 0x2d, 0x69, 0x33, 0x32, 0x00, 0x00, 0x0a,
            0x91, 0x80, 0x80, 0x80, 0x00, 0x01, 0x8b, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10, 0x05, 0x10, 0x03,
            0x40, 0x10, 0x03, 0x40, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { }), 11);
    }

    {
        // Generated from: (module (func (export "return-i32") (result i32) (block (return (i32.add (i32.add (i32.const 5) (i32.const 3)) (i32.const 3)))) (unreachable)) )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x85, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x00, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8e, 0x80, 0x80, 0x80,
            0x00, 0x01, 0x0a, 0x72, 0x65, 0x74, 0x75, 0x72, 0x6e, 0x2d, 0x69, 0x33, 0x32, 0x00, 0x00, 0x0a,
            0x95, 0x80, 0x80, 0x80, 0x00, 0x01, 0x8f, 0x80, 0x80, 0x80, 0x00, 0x00, 0x01, 0x00, 0x10, 0x05,
            0x10, 0x03, 0x40, 0x10, 0x03, 0x40, 0x09, 0x0f, 0x00, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { }), 11);
    }

    {
        // Generated from: (module (func (export "add") (param $x i32) (param $y i32) (result i32) (return (i32.add (get_local $x) (get_local $y)))) )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x87, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x8e, 0x80, 0x80, 0x80, 0x00,
            0x01, 0x88, 0x80, 0x80, 0x80, 0x00, 0x00, 0x14, 0x00, 0x14, 0x01, 0x40, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(1) }), 101);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(-1), box(1)}), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(std::numeric_limits<int>::max()), box(1) }), std::numeric_limits<int>::min());
    }

    {
        // Generated from:
        //    (module
        //     (func (export "locals") (param $x i32) (result i32) (local $num i32)
        //      (set_local $num (get_local $x))
        //      (return (get_local $num))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8a, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x06, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x73, 0x00, 0x00, 0x0a, 0x91, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x8b, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x14, 0x00, 0x15, 0x01, 0x14,
            0x01, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(10) }), 10);
    }

    {
        // Generated from:
        //    (module
        //     (func (export "sum") (param $x i32) (result i32) (local $y i32)
        //      (set_local $y (i32.const 0))
        //      (loop
        //       (block
        //        (br_if 0 (i32.eq (get_local $x) (i32.const 0)))
        //        (set_local $y (i32.add (get_local $x) (get_local $y)))
        //        (set_local $x (i32.sub (get_local $x) (i32.const 1)))
        //        (br 1)
        //        )
        //       )
        //      (return (get_local $y))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x86, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x87, 0x80, 0x80,
            0x80, 0x00, 0x01, 0x03, 0x73, 0x75, 0x6d, 0x00, 0x00, 0x0a, 0xae, 0x80, 0x80, 0x80, 0x00, 0x01,
            0xa8, 0x80, 0x80, 0x80, 0x00, 0x01, 0x01, 0x01, 0x10, 0x00, 0x15, 0x01, 0x02, 0x00, 0x01, 0x00,
            0x14, 0x00, 0x10, 0x00, 0x4d, 0x07, 0x00, 0x14, 0x00, 0x14, 0x01, 0x40, 0x15, 0x01, 0x14, 0x00,
            0x10, 0x01, 0x41, 0x15, 0x00, 0x06, 0x01, 0x0f, 0x0f, 0x14, 0x01, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2)}), 3);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100) }), 5050);
    }

    {
        // Generated from:
        //    (module
        //     (func (export "dumb-mult") (param $x i32) (param $y i32) (result i32) (local $total i32) (local $i i32) (local $j i32)
        //      (set_local $total (i32.const 0))
        //      (set_local $i (i32.const 0))
        //      (block (loop
        //              (br_if 1 (i32.eq (get_local $i) (get_local $x)))
        //              (set_local $j (i32.const 0))
        //              (set_local $i (i32.add (get_local $i) (i32.const 1)))
        //              (loop
        //               (br_if 1 (i32.eq (get_local $j) (get_local $y)))
        //               (set_local $total (i32.add (get_local $total) (i32.const 1)))
        //               (set_local $j (i32.add (get_local $j) (i32.const 1)))
        //               (br 0)
        //               )
        //              ))
        //      (return (get_local $total))
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x8d, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x09, 0x64, 0x75, 0x6d, 0x62, 0x2d, 0x6d, 0x75, 0x6c, 0x74, 0x00, 0x00,
            0x0a, 0xc7, 0x80, 0x80, 0x80, 0x00, 0x01, 0xc1, 0x80, 0x80, 0x80, 0x00, 0x01, 0x03, 0x01, 0x10,
            0x00, 0x15, 0x02, 0x10, 0x00, 0x15, 0x03, 0x01, 0x00, 0x02, 0x00, 0x14, 0x03, 0x14, 0x00, 0x4d,
            0x07, 0x01, 0x10, 0x00, 0x15, 0x04, 0x14, 0x03, 0x10, 0x01, 0x40, 0x15, 0x03, 0x02, 0x00, 0x14,
            0x04, 0x14, 0x01, 0x4d, 0x07, 0x01, 0x14, 0x02, 0x10, 0x01, 0x40, 0x15, 0x02, 0x14, 0x04, 0x10,
            0x01, 0x40, 0x15, 0x04, 0x06, 0x00, 0x0f, 0x0f, 0x0f, 0x14, 0x02, 0x09, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(1) }), 2);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2) }), 2);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(2) }), 4);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(6) }), 12);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(6) }), 600);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(100) }), 10000);
    }

    {
        // Generated from:
        //    (module
        //     (func "dumb-less-than" (param $x i32) (param $y i32) (result i32)
        //      (loop
        //       (block
        //        (block
        //         (br_if 0 (i32.eq (get_local $x) (get_local $y)))
        //         (br 1)
        //         )
        //        (return (i32.const 0))
        //        )
        //       (block
        //        (block
        //         (br_if 0 (i32.eq (get_local $x) (i32.const 0)))
        //         (br 1)
        //         )
        //        (return (i32.const 1))
        //        )
        //       (set_local $x (i32.sub (get_local $x) (i32.const 1)))
        //       (br 0)
        //       )
        //       (unreachable)
        //      )
        //     )
        Vector<uint8_t> vector = {
            0x00, 0x61, 0x73, 0x6d, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x87, 0x80, 0x80, 0x80, 0x00, 0x01, 0x40,
            0x02, 0x01, 0x01, 0x01, 0x01, 0x03, 0x82, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00, 0x07, 0x92, 0x80,
            0x80, 0x80, 0x00, 0x01, 0x0e, 0x64, 0x75, 0x6d, 0x62, 0x2d, 0x6c, 0x65, 0x73, 0x73, 0x2d, 0x74,
            0x68, 0x61, 0x6e, 0x00, 0x00, 0x0a, 0xb9, 0x80, 0x80, 0x80, 0x00, 0x01, 0xb3, 0x80, 0x80, 0x80,
            0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x14, 0x00, 0x14, 0x01, 0x4d, 0x07, 0x00, 0x06,
            0x01, 0x0f, 0x10, 0x00, 0x09, 0x0f, 0x01, 0x00, 0x01, 0x00, 0x14, 0x00, 0x10, 0x00, 0x4d, 0x07,
            0x00, 0x06, 0x01, 0x0f, 0x10, 0x01, 0x09, 0x0f, 0x14, 0x00, 0x10, 0x01, 0x41, 0x15, 0x00, 0x06,
            0x00, 0x0f, 0x00, 0x0f
        };

        Plan plan(*vm, vector);
        if (plan.result.size() != 1 || !plan.result[0]) {
            dataLogLn("Module failed to compile correctly.");
            CRASH();
        }

        // Test this doesn't crash.
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(0), box(1) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(0) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(2) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(2) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(1), box(1) }), 0);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(2), box(6) }), 1);
        CHECK_EQ(invoke<int>(*plan.result[0]->jsEntryPoint, { box(100), box(6) }), 0);
    }

}

#endif // ENABLE(WEBASSEMBLY)

int main(int argc, char** argv)
{
    CommandLine options(argc, argv);

    if (options.m_runLEBTests)
        runLEBTests();

    if (options.m_runWasmTests) {
#if ENABLE(WEBASSEMBLY)
        JSC::initializeThreading();
        vm = &JSC::VM::create(JSC::LargeHeap).leakRef();
        runWasmTests();
#else
        dataLogLn("Wasm is not enabled!");
        return EXIT_FAILURE;
#endif // ENABLE(WEBASSEMBLY)
    }

    return EXIT_SUCCESS;
}

