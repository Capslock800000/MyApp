#include <jni.h>
#include <cstdint>
#include <vector>

enum VMP_OP : uint8_t {
    VMP_PUSH = 0x01,
    VMP_ADD  = 0x02,
    VMP_SUB  = 0x03,
    VMP_MUL  = 0x04,
    VMP_XOR  = 0x05,
    VMP_RET  = 0xFF
};

struct VMP_CTX {
    int32_t stack[16];
    int sp = 0;
};

static int32_t vmpExec(const uint8_t* code, size_t len) {
    VMP_CTX ctx;
    size_t pc = 0;
    while (pc < len) {
        uint8_t op = code[pc++];
        switch (op) {
            case VMP_PUSH:
                ctx.stack[ctx.sp++] = (code[pc] << 24) | (code[pc+1] << 16) |
                                      (code[pc+2] << 8) | code[pc+3];
                pc += 4;
                break;
            case VMP_ADD:
                ctx.stack[ctx.sp - 2] += ctx.stack[ctx.sp - 1];
                ctx.sp--;
                break;
            case VMP_SUB:
                ctx.stack[ctx.sp - 2] -= ctx.stack[ctx.sp - 1];
                ctx.sp--;
                break;
            case VMP_XOR:
                ctx.stack[ctx.sp - 2] ^= ctx.stack[ctx.sp - 1];
                ctx.sp--;
                break;
            case VMP_RET:
                return ctx.stack[ctx.sp - 1];
            default:
                return 0;
        }
    }
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_myapp_security_NativeBridge_nativeVmpCalc(JNIEnv*, jobject, jint a, jint b) {
    uint8_t bytecode[] = {
        VMP_PUSH, 0, 0, 0, 0,
        VMP_PUSH, 0, 0, 0, 0,
        VMP_XOR,
        VMP_RET
    };
    bytecode[4] = a & 0xFF; bytecode[3] = (a >> 8) & 0xFF;
    bytecode[2] = (a >> 16) & 0xFF; bytecode[1] = (a >> 24) & 0xFF;
    bytecode[9] = b & 0xFF; bytecode[8] = (b >> 8) & 0xFF;
    bytecode[7] = (b >> 16) & 0xFF; bytecode[6] = (b >> 24) & 0xFF;
    return vmpExec(bytecode, sizeof(bytecode));
}
