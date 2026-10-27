"""
Apple Find My (Macless-Haystack) Anahtar Üretici
Bu betik, Apple Find My ağı ile uyumlu secp224r1 (NIST P-224) anahtar çifti üretir.
Üretilen 28 byte'lık Public Key dizisini doğrudan ESP32 kodundaki PUBLIC_KEY_28 içine yapıştırabilirsiniz.
Ayrıca Macless-Haystack / OpenHaystack için Base64 Private Key verir.
"""

import base64
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

def generate_findmy_keys():
    # 1. NIST P-224 (secp224r1) anahtar çifti oluştur
    private_key = ec.generate_private_key(ec.SECP224R1())
    public_key = private_key.public_key()

    # 2. Private Key (Raw 28 byte)
    priv_num = private_key.private_numbers().private_value
    priv_bytes = priv_num.to_bytes(28, byteorder='big')
    priv_b64 = base64.b64encode(priv_bytes).decode('utf-8')

    # 3. Public Key (Raw X koordinatı - 28 byte)
    pub_numbers = public_key.public_numbers()
    pub_x_bytes = pub_numbers.x.to_bytes(28, byteorder='big')
    pub_b64 = base64.b64encode(pub_x_bytes).decode('utf-8')

    # 4. BLE Random Static MAC Adresi (İlk 6 byte, üst 2 bit 11 yapılır)
    mac_bytes = bytearray(pub_x_bytes[:6])
    mac_bytes[0] |= 0xC0
    mac_str = ":".join(f"{b:02X}" for b in mac_bytes)

    # 5. C++ Array Formatı
    cpp_array = "const uint8_t PUBLIC_KEY_28[28] = {\n"
    cpp_array += "    " + ", ".join(f"0x{b:02X}" for b in pub_x_bytes[:6]) + ", // BLE Static MAC\n"
    cpp_array += "    " + ", ".join(f"0x{b:02X}" for b in pub_x_bytes[6:14]) + ",\n"
    cpp_array += "    " + ", ".join(f"0x{b:02X}" for b in pub_x_bytes[14:22]) + ",\n"
    cpp_array += "    " + ", ".join(f"0x{b:02X}" for b in pub_x_bytes[22:28]) + "   // BLE Payload\n"
    cpp_array += "};"

    print("=================================================================")
    print("   APPLE FIND MY (HAYSTACK) YENİ ANAHTAR ÇİFTİ ÜRETİLDİ")
    print("=================================================================")
    print(f"\n[1] C++ KODUNA YAPIŞTIRILACAK DİZİ (src/main.cpp):\n")
    print(cpp_array)
    print(f"\n[2] BLE MAC Adresi : {mac_str}")
    print(f"[3] Public Key (Base64) : {pub_b64}")
    print(f"[4] Private Key (Base64): {priv_b64}")
    print("\n* Not: Private Key (Base64) değerini Macless-Haystack veya OpenHaystack")
    print("  haritasında cihazınızı takip etmek için kaydedin!")
    print("=================================================================\n")

if __name__ == "__main__":
    generate_findmy_keys()
