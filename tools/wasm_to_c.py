import os

wasm_path = "external/wasm3/test/wasi/simple/test-opt.wasm"
h_path = "test_wasm.h"

if os.path.exists(wasm_path):
    with open(wasm_path, "rb") as f:
        data = f.read()
    
    with open(h_path, "w") as f:
        f.write("#ifndef TEST_WASM_H\n")
        f.write("#define TEST_WASM_H\n\n")
        f.write(f"// Generated from {wasm_path}\n")
        f.write(f"const unsigned int test_wasm_len = {len(data)};\n\n")
        f.write("const unsigned char test_wasm[] = {\n")
        
        # Write bytes in chunks of 12
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_str},\n")
            
        f.write("};\n\n")
        f.write("#endif // TEST_WASM_H\n")
    print(f"Generated {h_path} with {len(data)} bytes.")
else:
    print(f"Error: {wasm_path} not found.")
