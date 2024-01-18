#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define UTF16_BE 0b11
#define UTF16_LE 0b10
#define UTF8 0b0 

// assuming system uses little endian
struct {
   char *file_path;
   size_t file_size;
   int format;
} src;

struct {
   char *file_path;
   int format;
} dst;

size_t get_file_size(FILE *file) {
   fseek(file, 0, SEEK_END);
   size_t file_size_byte = ftell(file);
   rewind(file);
   return file_size_byte;
}

void swap_endian_2b(uint16_t *code_unit) {
   uint8_t LObyte = *code_unit;
   *code_unit = (*code_unit >> 8) & 0xff;
   *code_unit |= LObyte << 8;
}

void utf8_utf16(char *buffer) {
   uint16_t *to_write = (uint16_t*) malloc((src.file_size * 2) + 2);
   if (to_write == NULL) {
      perror("Error allocating memory -to_write-");
      exit(1);
   }

   *to_write = 0xfeff; // default encode as big endian
   size_t code_unit_count = 1; // one code unit = 2 bytes

   for (int i = 0; i < src.file_size; i++) {
      uint8_t byte = *(buffer + i);

      if (byte >= 0xf0) {
         uint16_t HO10BIT = 0b110110 << 10;
         uint16_t LO10BIT = 0b110111 << 10;

         int plane = 0;
         plane |= (buffer[i + 1] >> 4) & 0x3;
         plane |= (buffer[i] << 2) & 0x1c;
         plane -= 1;

         LO10BIT |= buffer[i + 3] & 0x3f;
         LO10BIT |= (buffer[i + 2] & 0xf) << 6;

         HO10BIT |= (buffer[i + 2] >> 4) & 0x3;
         HO10BIT |= (buffer[i + 1] << 2) & 0x3c;
         HO10BIT |= (plane << 6) & 0x3c0;

         *(to_write + code_unit_count) = LO10BIT;
         *(to_write + code_unit_count + 1) = HO10BIT;
         code_unit_count += 2;
      } else {
         if ((byte & 0xc0) == 0x80) { continue; }
         uint16_t UTF16BYTES = 0;

         if (byte > 0xe0) {
            UTF16BYTES |= buffer[i + 2] & 0x3f;
            UTF16BYTES |= (buffer[i + 1] & 0x3f) << 6;
            UTF16BYTES |= (buffer[i] & 0xf) << 12;
         } else if (byte > 0xc0) {
            UTF16BYTES |= buffer[i + 1] & 0x3f;
            UTF16BYTES |= (buffer[i] & 0x1f) << 6;
         } else {
            UTF16BYTES = byte;
         }
         *(to_write + code_unit_count) = UTF16BYTES;
         code_unit_count += 1;
      }
   }

   if (dst.format == UTF16_LE) {
      for (int i = 0; i < code_unit_count; i++) {
         swap_endian_2b(&to_write[i]);
      }
   }

   FILE *file = fopen(dst.file_path, "w");
   fwrite(to_write, code_unit_count * 2, 1, file);
   fclose(file);
   free(to_write);
}

void utf16_utf16(char *buffer) {
   // utf16 file size will always be even
   size_t code_unit_count = src.file_size / 2;

   uint16_t *uint16_buf = (uint16_t*) malloc(code_unit_count * 2);
   memcpy(uint16_buf, buffer, src.file_size);

   for (int i = 0; i < code_unit_count; i++) {
      swap_endian_2b(&uint16_buf[i]);
   }

   FILE *file = fopen(dst.file_path, "w");
   fwrite(uint16_buf, code_unit_count * 2, 1, file);
   fclose(file);
   free(uint16_buf);
}

void utf16_utf8(char *buffer) {
   uint16_t code_unit_count = src.file_size / 2;
   
   uint16_t *uint16buf = (uint16_t*) malloc(src.file_size);
   memcpy(uint16buf, buffer, src.file_size);
   
   // memcpy swaps byte (on little endian systems)
   if (src.format == UTF16_BE) {
      for (int i = 0; i < code_unit_count; i++) {
         swap_endian_2b(&uint16buf[i]);
      }
   }

   uint32_t *codepointbuf = (uint32_t*) malloc(src.file_size * 2);
   size_t cp_count = 0;
   for (int i = 1; i < code_unit_count; i++) {
      uint16_t code_unit = uint16buf[i]; 
      if ((code_unit & 0xFC00) == 0xD800) {
         uint16_t HOcodepoint = (code_unit & 0x03FF) + 0x40;
         codepointbuf[cp_count] = HOcodepoint << 10;
      } 
      else if ((code_unit & 0xFC00) == 0xDC00) {
         codepointbuf[cp_count++] |= code_unit & 0x03FF;
      } 
      else {
         codepointbuf[cp_count++] = code_unit;
      }
   }
   free(uint16buf);

   uint8_t *uint8buf = (uint8_t*) malloc(src.file_size * 1.5);
   size_t byte_count = 0;
   for (int i = 0; i < cp_count; i++) {
      uint32_t codepoint = codepointbuf[i];
      if (codepoint < 0x7F) {
         uint8buf[byte_count++] = codepoint; 
      } 
      else if (codepoint < 0x7FF) {
         uint8buf[byte_count++] = (codepoint >> 6) | 0b11000000;
         uint8buf[byte_count++] = (codepoint & 0x3F) | 0b10000000;
      } 
      else if (codepoint < 0xFFFF) {
         uint8buf[byte_count++] = (codepoint >> 12) | 0b11100000;
         uint8buf[byte_count++] = ((codepoint >> 6) & 0x3F) | 0b10000000;
         uint8buf[byte_count++] = (codepoint & 0x3F) | 0b10000000;
      } else {
         uint8buf[byte_count++] = (codepoint >> 18) | 0b11110000;
         uint8buf[byte_count++] = ((codepoint >> 12) & 0x3F) | 0b10000000;
         uint8buf[byte_count++] = ((codepoint >> 6) & 0x3F) | 0b10000000;
         uint8buf[byte_count++] = (codepoint & 0x3F) | 0b10000000;
      }
   }

   free(codepointbuf);
   FILE *file = fopen(dst.file_path, "w");
   fwrite(uint8buf, byte_count, 1, file);
   free(uint8buf);
}

void conversion(char *buffer) {
   if ((uint8_t)buffer[0] == 0xFF && (uint8_t)buffer[1] == 0xFE) 
      { src.format = UTF16_LE; }
   else if ((uint8_t)buffer[0] == 0xFE && (uint8_t)buffer[1] == 0xFF) 
      { src.format = UTF16_BE; }
   else 
      { src.format = UTF8; } 

   dst.format = UTF8;

   if (src.format == UTF8 && (dst.format == UTF16_LE || dst.format == UTF16_BE)) {
      utf8_utf16(buffer);
   }

   else if (src.format ^ dst.format == UTF16_BE ^ UTF16_LE) {
      utf16_utf16(buffer);
   }

   else if ((src.format == UTF16_LE || src.format == UTF16_BE) && dst.format == UTF8) {
      utf16_utf8(buffer);
   }
}

int main() {
   src.file_path = "./test-utf16";
   dst.file_path = "./testt";

   char* buffer;

   FILE *file = fopen(src.file_path, "r+");
   if (file == NULL) {
      perror("Error opening file");
      return 1;
   }
   src.file_size = get_file_size(file);

   buffer = (char*)malloc(src.file_size);
   fread(buffer, src.file_size, 1, file);
   fclose(file);

   conversion(buffer);
   free(buffer);

   return 0;
}