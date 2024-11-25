#include "header.h"

// The assembler object and the pointer through which it is accessed
ASSEMBLER assembler;
ASSEMBLER *as;

const uint8_t asm_opcode[56][11] = {
    { -1  , 0x6d, 0x7d, 0x79, 0x69, 0x61, 0x71, 0x65, 0x75, -1   } , /*  0: ADC */
    { -1  , 0x2d, 0x3d, 0x39, 0x29, 0x21, 0x31, 0x25, 0x35, -1   } , /*  1: AND */
    { 0x0a, 0x0e, 0x1e, -1  , -1  , -1  , -1  , 0x06, 0x16, -1   } , /*  2: ASL */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x90, -1  , -1   } , /*  3: BCC */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xb0, -1  , -1   } , /*  4: BCS */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xf0, -1  , -1   } , /*  5: BEQ */
    { -1  , 0x2c, -1  , -1  , -1  , -1  , -1  , 0x24, -1  , -1   } , /*  6: BIT */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x30, -1  , -1   } , /*  7: BMI */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0xd0, -1  , -1   } , /*  8: BNE */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x10, -1  , -1   } , /*  9: BPL */
    { 0x00, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 10: BRK */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x50, -1  , -1   } , /* 11: BVC */
    { -1  , -1  , -1  , -1  , -1  , -1  , -1  , 0x70, -1  , -1   } , /* 12: BVS */
    { 0x18, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 13: CLC */
    { 0xd8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 14: CLD */
    { 0x58, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 15: CLI */
    { 0xb8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 16: CLV */
    { -1  , 0xcd, 0xdd, 0xd9, 0xc9, 0xc1, 0xd1, 0xc5, 0xd5, -1   } , /* 17: CMP */
    { -1  , 0xec, -1  , -1  , 0xe0, -1  , -1  , 0xe4, -1  , -1   } , /* 18: CPX */
    { -1  , 0xcc, -1  , -1  , 0xc0, -1  , -1  , 0xc4, -1  , -1   } , /* 19: CPY */
    { -1  , 0xce, 0xde, -1  , -1  , -1  , -1  , 0xc6, 0xd6, -1   } , /* 20: DEC */
    { 0xca, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 21: DEX */
    { 0x88, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 22: DEY */
    { -1  , 0x4d, 0x5d, 0x59, 0x49, 0x41, 0x51, 0x45, 0x55, -1   } , /* 23: EOR */
    { -1  , 0xee, 0xfe, -1  , -1  , -1  , -1  , 0xe6, 0xf6, -1   } , /* 24: INC */
    { 0xe8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 25: INX */
    { 0xc8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 26: INY */
    { -1  , 0x4c, -1  , -1  , 0x6C, -1  , -1  , -1  , -1  , -1   } , /* 27: JMP */
    { -1  , 0x20, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 28: JSR */
    { -1  , 0xad, 0xbd, 0xb9, 0xa9, 0xa1, 0xb1, 0xa5, 0xb5, -1   } , /* 29: LDA */
    { -1  , 0xae, -1  , 0xbe, 0xa2, -1  , -1  , 0xa6, -1  , 0xb6 } , /* 30: LDX */
    { -1  , 0xac, 0xbc, -1  , 0xa0, -1  , -1  , 0xa4, 0xb4, -1   } , /* 31: LDY */
    { 0x4a, 0x4e, 0x5e, -1  , -1  , -1  , -1  , 0x46, 0x56, -1   } , /* 32: LSR */
    { 0xea, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 33: NOP */
    { -1  , 0x0d, 0x1d, 0x19, 0x09, 0x01, 0x11, 0x05, 0x15, -1   } , /* 34: ORA */
    { 0x48, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 35: PHA */
    { 0x08, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 36: PHP */
    { 0x68, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 37: PLA */
    { 0x28, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 38: PLP */
    { 0x2a, 0x2e, 0x3e, -1  , -1  , -1  , -1  , 0x26, 0x36, -1   } , /* 39: ROL */
    { 0x6a, 0x6e, 0x7e, -1  , -1  , -1  , -1  , 0x66, 0x76, -1   } , /* 40: ROR */
    { 0x40, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 41: RTI */
    { 0x60, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 42: RTS */
    { -1  , 0xed, 0xfd, 0xf9, 0xe9, 0xe1, 0xf1, 0xe5, 0xf5, -1   } , /* 43: SBC */
    { 0x38, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 44: SEC */
    { 0xf8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 45: SED */
    { 0x78, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 46: SEI */
    { -1  , 0x8d, 0x9d, 0x99, -1  , 0x81, 0x91, 0x85, 0x95, -1   } , /* 47: STA */
    { -1  , 0x8e, -1  , -1  , -1  , -1  , -1  , 0x86, -1  , 0x96 } , /* 48: STX */
    { -1  , 0x8c, -1  , -1  , -1  , -1  , -1  , 0x84, 0x94, -1   } , /* 49: STY */
    { 0xaa, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 50: TAX */
    { 0xa8, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 51: TAY */
    { 0xba, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 52: TSX */
    { 0x8a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 53: TXA */
    { 0x9a, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 54: TXS */
    { 0x98, -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1  , -1   } , /* 55: TYA */
};

const char *address_mode_txt[] = {
    "ACCUMULATOR/IMPLIED",
    "ABSOLUTE",
    "ABSOLUTE,X",
    "ABSOLUTE,Y",
    "IMMEDIATE",
    "(INDIRECT,X)",
    "(INDIRECT),Y",
    "ZEROPAGE/RELATIVE",
    "ZEROPAGE,X",
    "ZEROPAGE,Y",
};

//----------------------------------------------------------------------------
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
uint32_t fnv_1a_hash(const char *key, size_t len) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;  // FNV prime
    }
    return hash;
}

//----------------------------------------------------------------------------
// Include file stack management
void include_files_cleanup() {
    size_t i;
    // discard all the files
    for(i = 0; i < as->include_files.included_files.items; i++) {
        UTIL_FILE *included_file = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, i);
        util_file_discard(included_file);
    }
    // Discard the arrays
    array_free(&as->include_files.included_files);
    array_free(&as->include_files.stack);
}

UTIL_FILE *include_files_find_file(const char *file_name) {
    size_t i;
    // Search for a matching file by name
    for(i = 0; i < as->include_files.included_files.items; i++) {
        UTIL_FILE *f = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, i);
        if(0 == stricmp(f->file_path, file_name)) {
            return f;
        }
    }
    return NULL;
}

void include_files_init() {
    ARRAY_INIT(&as->include_files.included_files, UTIL_FILE);
    ARRAY_INIT(&as->include_files.stack, PARSE_DATA);
}

int include_files_pop() {
    // when the last item is popped, there is nothing to "pop to" so error
    if(as->include_files.stack.items <= 1) {
        as->include_files.stack.items = 0;
        return A2_ERR;
    }
    // Pop to the previous item on the stack
    as->include_files.stack.items--;
    PARSE_DATA *pd = ARRAY_GET(&as->include_files.stack, PARSE_DATA, as->include_files.stack.items);
    as->current_file = pd->file_name;
    as->token_start = as->input = pd->input;
    as->current_line = pd->line_number;
    return A2_OK;
}

int include_files_push(const char *file_name) {
    int recursive_include = 0;
    UTIL_FILE new_file;

    // See if the file had previously been loaded
    UTIL_FILE *f = include_files_find_file(file_name);
    if(!f) {
        // If not, it's a new file, load it
        memset(&new_file, 0, sizeof(UTIL_FILE));
        new_file.load_padding = 1;
        
        if(A2_OK == util_file_load(&new_file, file_name, "r")) {
            // Success, add tp list of laoded files and assign it to f
            if(A2_OK != ARRAY_ADD(&as->include_files.included_files, new_file)) {
                errlog("Out of memory");
            }
            f = &new_file;
        }
    } else {
        size_t i;
        // See if previously loaded file is in the stack - then this is a recursive include
        for(i = 0; i < as->include_files.stack.items; i++) {
            PARSE_DATA *pd = ARRAY_GET(&as->include_files.stack, PARSE_DATA, i);
            if(0 == stricmp(pd->file_name, file_name)) {
                recursive_include = 1;
                break;
            }
        }
    }

    if(!f) {
        errlog("Error loading file %s", file_name);
        return A2_ERR;
    } else if(recursive_include) {
        errlog("Recursive included of file %s ignored", file_name);
        return A2_ERR;
    }

    // Push the file onto the stack, documenting the current parse data
    PARSE_DATA pd;
    pd.file_name = as->current_file;
    pd.input = as->input;
    pd.line_number = as->current_line;
    as->current_file = f->file_path;

    if(A2_OK != ARRAY_ADD(&as->include_files.stack, pd)) {
        errlog("Out of memory");
    }

    // Prepare to parse the buffer that has now become active
    as->current_file = f->file_path;
    as->line_start = as->token_start = as->input = f->file_data;
    as->next_line_count = 0;
    as->current_line = 1;
    return A2_OK;
}

//----------------------------------------------------------------------------
// Output
void emit(uint8_t byte_value) {
    if(as->pass == 2) {
        write_to_memory(as->m, as->current_address++, byte_value);
        as->last_address = as->current_address;
    } else {
        as->current_address++;
    }
}

void write_opcode() {
    int8_t opcode = asm_opcode[as->opcode_info.opcode_id][as->opcode_info.addressing_mode];
    if(opcode == -1) {
        errlog("Invalid opcode %.3s with mode %s", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
    }
    // First the opcode
    emit(opcode);
    // Then the operand (0, ie implied, will do nothing more)
    switch(as->opcode_info.width) {
        case 1: // Relative - 1 byte
            {
                int32_t delta = as->opcode_info.value - 1 - as->current_address;
                if(delta > 128 || delta < -128) {
                    errlog("Relative branch out of range $%X", delta);
                }
                emit(delta);
            }
            break;
        case 8: // 1 byte
            if(as->opcode_info.value >= 256) {
                errlog("8-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit(as->opcode_info.value);
            break;
        case 16:    // 2 bytes
            if(as->opcode_info.value >= 65536) {
                errlog("16-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit(as->opcode_info.value);
            emit(as->opcode_info.value >> 8);
            break;
    }
}

void write_bytes(uint64_t value, int width, int order) {
    if(order == BYTE_ORDER_HI) {
        if(width == 8) {
            emit(value);
        } else if(width == 16) {
            emit(value >> 8);
            emit(value);
        } else if(width == 32) {
            emit(value >> 24);
            emit(value >> 16);
            emit(value >> 8);
            emit(value);
        } else if(width == 64) {
            emit(value >> 56);
            emit(value >> 48);
            emit(value >> 40);
            emit(value >> 32);
            emit(value >> 24);
            emit(value >> 16);
            emit(value >> 8);
            emit(value);
        }
    } else {
        if(width == 8) {
            emit(value);
        } else if(width == 16) {
            emit(value);
            emit(value >> 8);
        } else if(width == 32) {
            emit(value);
            emit(value >> 8);
            emit(value >> 16);
            emit(value >> 24);
        } else if(width == 64) {
            emit(value);
            emit(value >> 8);
            emit(value >> 16);
            emit(value >> 24);
            emit(value >> 32);
            emit(value >> 40);
            emit(value >> 48);
            emit(value >> 56);
        }
    }
}

void write_values(int width, int order) {
    do {
        write_bytes(evaluate_expression(), width, order);
    } while(as->current_token.op == ',');
}

//----------------------------------------------------------------------------
// Symbol storage / lookup
int anonymous_symbol_lookup(uint16_t *address, int direction) {
    // Ensure the array has items
    if (as->anon_symbols.items == 0) {
        *address = 0xFFFF;
        return 0; // Not found
    }

    int low = 0;
    int high = (int)as->anon_symbols.items - 1;
    int exact_match_index = -1;
    int closest_smaller_index = -1;
    int target_index;

    // Perform binary search to find the closest smaller or equal label
    while (low <= high) {
        int mid = (low + high) / 2;
        uint16_t value = *ARRAY_GET(&as->anon_symbols, uint16_t, mid);

        if (value == *address) {
            exact_match_index = mid; // Exact match found, not sure this can happen?
            break;
        } else if (value < *address) {
            closest_smaller_index = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (exact_match_index != -1) {
        // If we have an exact match
        target_index = exact_match_index + direction;
    } else {
        // Determine the target_index with direction
        if (direction < 0) {
            // For negative direction: Start at the label greater than
            // the closest_smaller_index
            target_index = closest_smaller_index + 1 + direction;
        } else {
            // For zero or positive direction: Start at the closest_smaller_index
            target_index = closest_smaller_index + direction;
        }
    }

    // Check bounds
    if (target_index < 0 || target_index >= (int)as->anon_symbols.items) {
        // Error out of bounds
        *address = 0xFFFF;
        return 0;
    }

    // Return the address at the target index
    *address = *ARRAY_GET(&as->anon_symbols, uint16_t, target_index);
    return 1;
}

int symbol_sort(const void* lhs, const void* rhs) {
   return (uint16_t)(((SYMBOL_LABEL*)lhs)->symbol_value) - (uint16_t)(((SYMBOL_LABEL*)rhs)->symbol_value);
}

SYMBOL_LABEL *symbol_store(const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    uint32_t name_hash = fnv_1a_hash(symbol_name, symbol_name_length);
    SYMBOL_LABEL *sl = symbol_lookup(name_hash, symbol_name, symbol_name_length);
    if(sl) {
        if(sl->symbol_type == SYMBOL_UNKNOWN) {
            // Forward referenced variables are assigned a type and value when resolved
            // The width is left unchanged (16 bit was assumed).  The width can't change
            // between pass 1 & 2 or all other labels will be off, or need to be adjusted
            sl->symbol_type = symbol_type;
            sl->symbol_value = value;
        } else {
            if(sl->symbol_type != symbol_type) {
                // Symbol changing type error
                errlog("Symbol %.*s can't be address and variable type", symbol_name_length, symbol_name);
            }
            if(sl->symbol_type == SYMBOL_VARIABLE) {
                // Variables can change value along the way
                sl->symbol_value = value;
            } else if(sl->symbol_value != value) {
                // Addresses may not change value
                errlog("Multiple address labels have name %.*s", symbol_name_length, symbol_name);
            }
        }
    } else {
        // Create a new entry for a previously unknown symbol
        SYMBOL_LABEL new_sl;
        new_sl.symbol_type = symbol_type;
        new_sl.symbol_hash = name_hash;
        new_sl.symbol_length = symbol_name_length;
        new_sl.symbol_name = symbol_name;
        new_sl.symbol_value = value;
        new_sl.symbol_width = (value <= UINT8_MAX) ? 8 : (value <= UINT16_MAX) ? 16 : (value <= UINT32_MAX) ? 32 : 64;
        uint8_t bucket = name_hash & 0xff;
        DYNARRAY *bucket_array = ARRAY_GET(&as->symbol_table, DYNARRAY, bucket);
        ARRAY_ADD(bucket_array, &new_sl);
        sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, bucket_array->items - 1);
    }
    return sl;
}

SYMBOL_LABEL *symbol_lookup(uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    uint8_t bucket = name_hash & 0xff;
    DYNARRAY *bucket_array = ARRAY_GET(&as->symbol_table, DYNARRAY, bucket);
    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
            return sl;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// Token helpers
char character_in_characters(const char character, const char *characters) {
    while(*characters && character != *characters) {
        characters++;
    }
    return *characters;
}

void expect(char op) {
    if(as->current_token.type != TOKEN_OP || as->current_token.op != op) {
        errlog("Expected '%c'", op);
    }
    next_token();
}

const char *find_delimiter(const char *delimitors) {
    while(as->token_start < as->input) {
        const char *c = delimitors;
        while(*c && *as->token_start != *c) {
            c++;
        }
        if(*c) {
            return as->token_start;
        }
        as->token_start++;
    }
    return 0;
}

int match(int number, ...) {
    va_list args;
    va_start(args, number);

    // For all match arguments
    for(int i = 0; i < number; i++) {
        int j = 0;
        // Get the argument
        const char *str = va_arg(args, const char *);
        // Compare token start to the argument string
        while(*str && as->token_start[j] && *str == as->token_start[j]) {
            str++;
            j++;
        }
        if(!(*str)) {
            // If !*str it was a match
            as->token_start += j;
            va_end(args);
            return i;
        }
    }
    va_end(args);
    return -1;
}

//----------------------------------------------------------------------------
// Assembly generator helpers
void decode_abs_rel_zp_opcode() {
    // Relative (branch) instructions have their opcodes in zero page as well
    int relative = as->opcode_info.width == 1;
    as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE;
    if(!relative) {
        // Not relative, so may be absolute based on operand size
        if(as->opcode_info.value >= 256 || as->opcode_info.width > 8) {
            as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE;
            as->opcode_info.width = 16;
        }
        if(as->current_token.op == ',') {
            // Make sure a comma is followed by x or y
            get_token();
            switch(tolower(*as->token_start)) {
                case 'x':
                    as->opcode_info.addressing_mode++;
                    break;
                case 'y':
                    as->opcode_info.addressing_mode += 2;
                    break;
                default:
                    errlog("Unexpected ,%c", *as->token_start);
                    break;
            }
        }
    }
    write_opcode();
}

int is_indexed_indirect(char *reg) {
    const char *c = as->token_start;
    int brackets = 1;
    // Only called when it's known there are ()'s on the line
    // Scan forward counting brackets and looking for a ,
    while(*c && *c != ',' && *c != ';' && !is_newline(*c)) {
        if(*c == '(') {
            brackets++;
        } else if(*c == ')') {
            brackets--;
        }
        c++;
    }
    // No comma, not indexed
    if(*c == ',') {
        // Comma in bracket is x-indexed
        if(brackets != 0) {
            *reg = 'x';
            return ADDRESS_MODE_INDIRECT_X;
        } else {
            // Comma outside brackets is y-indexed
            *reg = 'y';
            return ADDRESS_MODE_INDIRECT_Y;
        }
    }
    return 0;
}

//----------------------------------------------------------------------------
// Token identification
int is_address() {
    if(*as->token_start != '*') {
        return 0;
    }
    return 1;
}

int is_dot_command() {
    OpcodeInfo *oi;
    if(*as->token_start != '.' || !(oi = in_word_set(as->token_start, as->input - as->token_start))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_label() {
    if(*(as->input-1) != ':') {
        return 0;
    }
    return 1;
}

int is_newline(char c) {
    return (c == '\n' || c == '\r');
}

int is_opcode() {
    OpcodeInfo *oi;
    if(as->input - as->token_start != 3 || *as->token_start == '.' || !(oi = in_word_set(as->token_start, 3))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_variable() {
    // Variable start with [a-Z] or _
    if(*as->token_start != '_' && !isalpha(*as->token_start)) {
        return 0;
    }
    const char *c = as->token_start;
    // an can contain same plus [0-9]
    while(c < as->input && (*c == '_' || isalnum(*c))) {
        c++;
    }
    if(c != as->input) {
        return 0;
    }
    return 1;
}

int is_valid_instruction_only() {
    switch(as->opcode_info.opcode_id) {
        case GPERF_OPCODE_ASL:
        case GPERF_OPCODE_LSR:
        case GPERF_OPCODE_ROL:
        case GPERF_OPCODE_ROR:
            // is it just the 3 letters
            // or does an a follow (bi itelf or a;) 
            // SQW - do this differently - consume token
            return (as->input == as->token_start) ||
            ((*as->token_start == 'a' || *as->token_start == 'A') && 
                (isspace((*(as->token_start+1))) || *(as->token_start+1) == ';'));

        default:
            if(!as->opcode_info.width) {
                return 1;
            }
    }
    return 0;
}

//----------------------------------------------------------------------------
void parse_dot_endfor() {
    if(as->loop_stack.items) {
        const char *post_loop = as->input;
        FOR_LOOP *for_loop = ARRAY_GET(&as->loop_stack, FOR_LOOP, as->loop_stack.items-1);
        int same_file = stricmp(for_loop->loop_start_file, as->current_file) == 0;
        as->token_start = as->input = for_loop->loop_adjust_start;
        evaluate_expression();
        as->token_start = as->input = for_loop->loop_condition_start;
        if(same_file && evaluate_expression()) {
            as->token_start = as->input = for_loop->loop_body_start;
            as->current_line = for_loop->body_line;
        } else {
            // Pop the for loop from the stack
            as->loop_stack.items--;
            as->token_start = as->input = post_loop;
            if(!same_file) {
                errlog(".endfor matches .for in file %s, body at line %zd", for_loop->loop_start_file, for_loop->body_line);
            }
            as->current_line--;
        }
    } else {
        errlog(".endfor without a matching .for");
    }
}

void parse_dot_for() {
    FOR_LOOP for_loop;
    evaluate_expression(); // assignment
    for_loop.loop_condition_start = as->input;
    for_loop.loop_start_file = as->current_file;
    // evaluate the condition
    if(!evaluate_expression()) {
        // failed so find .endfor (must be in same file)
        do {
            get_token();
        } while(*as->input && (as->token_start == as->input || strnicmp(".endfor", as->token_start, 7)));
        if(!*as->input) {
            errlog(".for without .endfor");
        } else {
            // skip past .endfor
            next_token();
        }
    } else {
        // condition success
        for_loop.loop_adjust_start = as->input;
        // skip past the adjust condition
        do {
            get_token();
        } while(*as->input && as->token_start != as->input);
        // to find the loop body
        for_loop.loop_body_start = as->input;
        for_loop.body_line = as->current_line + as->next_line_count;
    }
    ARRAY_ADD(&as->loop_stack, for_loop);
}

void process_dot_include() {
    get_token();
    if(*as->token_start != '"') {
        errlog("include expects a \" enclosed string as a parameter");
    } else {
        char file_name[PATH_MAX];
        size_t string_length = as->input - as->token_start - 2;
        strncpy(file_name, as->token_start + 1, string_length);
        file_name[string_length] = '\0';
        include_files_push(file_name);
    }
}

void process_dot_org() {
    uint64_t value = evaluate_expression();
    if(as->current_address > value) {
        errlog("Assigning address %04X when address is already %04X error", value, as->current_address);
    } else {
        as->current_address = value;
        if(value < as->start_address) {
            as->start_address = value;
        }
    }
}

void process_dot_strcode() {
    if(as->pass == 2) {
        int insertions = 0;
        int count_x = 0;
        // Clear out any previous code
        free(as->strcode);
        as->strcode = 0;
        free(as->strcode_work);
        as->strcode_work = 0;
        get_token();
        const char *c = as->token_start;
        // Count the _'s
        while(!character_in_characters(as->current_token.op, ";\n\r")) {
            while(as->token_start < as->input) {
                if(*as->token_start == '_') {
                    insertions++;
                    while(as->token_start < as->input && *as->token_start == '_') {
                        count_x++;
                        as->token_start++;
                    }
                }
                as->token_start++;
            }
            get_token();
        }
        if(!insertions) {
            // An strcode with no replacements is meaningless
            errlog(".strcode requires an _ for character replacement");
        }
        // The string length - extra _'s + space for 3 _'s per replacement + '\0'
        int strcode_len = as->input - c - count_x + 3 * insertions + 1;
        // A value of 4 with one _ means no transform, so leave as->strcode as null
        if(!(insertions == 1 && strcode_len == 4)) {
            as->strcode = (char*)malloc(strcode_len);
            if(!as->strcode) {
                errlog("Out of memory");
            }
            // Now copy the expression, but expanding _ to ___
            char *d = as->strcode;
            insertions = 0;
            while(c < as->input) {
                *d++ = *c;
                if(*c == '_') {
                    c++;
                    while(c < as->input && *c == '_') {
                        c++;
                    }
                    // Add 2 more x's to the code (total 3 for number up to 255 ie 3 digits)
                    *d++ = '_';
                    *d++ = '_';
                } else {
                    c++;
                }
            }
            // Terminate with a null
            *d = '\0';
            // Make a work string (as->strcode is the template)
            as->strcode_work = strdup(as->strcode);
            if(!as->strcode_work) {
                errlog("Out of memory");
            }
        }
    } else {
        get_token();
        while(!character_in_characters(as->current_token.op, ";\n\r")) {
            get_token();
        }
    }
}

void process_dot_string() {
    uint64_t value;
    do {
        // Get to a token to process
        next_token();
        // String data
        if(as->current_token.type == TOKEN_OP && as->current_token.op == '"') {
            // Loop over the string but stop before the closing quote
            while(as->token_start < (as->input - 1)) {
                // Handle quoted numbers (no \n or \t handling here)
                if(*as->token_start == '\\') {
                    as->token_start++;
                    if(*as->token_start == '\\') {
                        value = '\\';
                        as->token_start++;
                    } else if(tolower(*as->token_start) == 'x') {
                        as->token_start++;
                        value = strtoll(as->token_start, (char**)&as->token_start, 16);
                    } else if(*as->token_start == '%') {
                        as->token_start++;
                        value = strtoll(as->token_start, (char**)&as->token_start, 2);
                    } else if(*as->token_start == '0') {
                        value = strtoll(as->token_start, (char**)&as->token_start, 8);
                    } else {
                        value = strtoll(as->token_start, (char**)&as->token_start, 10);
                    }
                    if(value >= 256) {
                        errlog("Escape value %ld not between 0 and 255", value);
                    }
                    emit(value);
                } else {
                    // Regular characters go through the strcode process
                    int character = *as->token_start;
                    if(as->strcode_work) {
                        int i = 0;
                        while(as->strcode[i]) {
                            if(as->strcode[i] == '_') {
                                char temp[4];
                                sprintf(temp ,"%3d", character);
                                memcpy(&as->strcode_work[i], temp, 3);
                                i += 2;
                            }
                            i++;
                        }
                        // This is a sad hack to use evaluate_ to
                        // process the strcode expression with _'s
                        // filled in by the character from the string
                        const char *ts = as->token_start;
                        const char *in = as->input;
                        as->input = as->token_start = as->strcode_work;
                        character = evaluate_expression();
                        as->token_start = ts;
                        as->input = in;
                    }
                    emit(character);
                    as->token_start++;
                }
            }
            next_token();
        } else {
            // Non-string data must be byte expression (and 1st token already
            // loaded so use parse_ not evaluate_)
            value = parse_expression();
            emit(value);
            if(value >= 256) {
                errlog("Values %ld not between 0 and 255", value);
            }
        }
    } while(as->current_token.op == ',');
}

//----------------------------------------------------------------------------
// Non-expression Parse routines
void parse_address() {
    // Address assignment like `* = EXPRESSION` or modification `* += EXPRESSION`
    uint16_t address;
    int op;
    get_token();
    if(-1 == (op = match(3, "+=", "+", "="))) {
        errlog("Address assign error");
        return;
    }
    if(0 == op) {
        // += returned just the + so get the ='s
        next_token();
    }
    int64_t value = evaluate_expression();
    switch(op) {
        case 0:
        case 1:
            address = as->current_address + value;
            break;
        case 2:
            address = value;
            break;
    }
    if(as->current_address > address) {
        errlog("Assigning address %04X when address is already %04X error", address, as->current_address);
    } else {
        as->current_address = address;
        if(address < as->start_address) {
            as->start_address = address;
        }
    }
}

uint16_t parse_anonymous_address() {
    // Anonymous label
    next_token();
    char op = as->current_token.op;
    int direction = 0;
    while(op == as->current_token.op && as->current_token.type == TOKEN_OP) {
        direction++;
        next_token();
    }
    switch(op) {
        case '+':
            break;
        case '-':
            direction = -direction;
            break;
        defailt:
            errlog("Unexpected symbol after anonymoys : (%c)", op);
            break;
    }
    // The opcode has not been emitted so + 1
    uint16_t address = as->current_address + 1;
    if(!anonymous_symbol_lookup(&address, direction)) {
        errlog("Invalid anonymous label address");
    }
    return address;
}

void parse_dot_command() {
    switch(as->opcode_info.opcode_id) {
        case GPERF_DOT_ALIGN:
            uint64_t value = evaluate_expression();
            as->current_address = (as->current_address + (value - 1)) & ~(value - 1);
            break;
        case GPERF_DOT_BYTE:
            write_values(8, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_DROW:
            write_values(16, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWD:
            write_values(32, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWQ:
            write_values(64, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DWORD:
            write_values(32, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_ENDFOR:
            parse_dot_endfor();
            break;
        case GPERF_DOT_FOR:
            parse_dot_for();
            break;
        case GPERF_DOT_INCLUDE:
            process_dot_include();
            break;
        case GPERF_DOT_ORG:
            process_dot_org();
            break;
        case GPERF_DOT_OUTPUT:
            break;
        case GPERF_DOT_QWORD:
            write_values(64, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_SAVEAS:
            break;
        case GPERF_DOT_STRCODE:
            process_dot_strcode();
            break;
        case GPERF_DOT_STRING:
            process_dot_string();
            break;
        case GPERF_DOT_WORD:
            write_values(16, BYTE_ORDER_LO);
            break;
    }
}

void parse_label() {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - 1 - as->token_start;
    if(!name_length) {
        // No name = anonymous label
        if(as->pass == 1) {
            // Only add anonymous labels in pass 1 (so there's no doubling up)
            ARRAY_ADD(&as->anon_symbols, as->current_address);
        }
    } else {
        symbol_store(symbol_name, name_length, SYMBOL_ADDRESS, as->current_address);
    }
}

void parse_opcode() {
    next_token();
    if(is_valid_instruction_only()) {
        // Implied
        emit(asm_opcode[as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR]);
    } else {
        int processed = 0;
        switch(as->current_token.op) {
            case '#':
                // Immidiate
                as->opcode_info.value = evaluate_expression();
                as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
                write_opcode();
                processed = 1;
                break;
            case '(':
                if(as->opcode_info.opcode_id == GPERF_OPCODE_JMP) {
                    // jmp (INDIRECT) is a special case
                    as->opcode_info.value = parse_expression();
                    as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
                    write_opcode();
                    processed = 1;
                } else {
                    char reg;
                    int indexed_indirect = is_indexed_indirect(&reg);
                    if(indexed_indirect) {
                        // Already inside bracket
                        if(reg == 'x') {
                            // , in ,x ends expression so evaluate to ignore active open (
                            as->opcode_info.value = evaluate_expression();
                        } else {
                            // start with the "active" ( and end in ) so parse
                            as->opcode_info.value = parse_expression();
                        }
                        as->opcode_info.addressing_mode = indexed_indirect;
                        write_opcode();
                        next_token();
                        // Make sure it was ,x or ,y
                        if(tolower(*as->token_start) != reg) {
                            errlog("Expected ,%c", reg);
                        }
                        next_token();
                        // If it was ,x go past the closing )
                        if(as->current_token.op == ')') {
                            next_token();
                        }
                        processed = 1;
                    }
                }
                break;
        }
        if(!processed) {
            // general case
            as->opcode_info.value = parse_expression();
            if(as->opcode_info.width >= 8 && as->expression_size > 8) {
                as->opcode_info.width = as->expression_size;
            }
            decode_abs_rel_zp_opcode();
        }
    }
}

void parse_variable() {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - as->token_start;
    next_token();
    expect('=');
    symbol_store(symbol_name, name_length, SYMBOL_VARIABLE, parse_expression());
}

//----------------------------------------------------------------------------
// Assembler start and end routines
int assembler_init(const char *starting_file) {
    as->current_file = starting_file;

    memset(as->m->RAM_MAIN, 0, 64*1024);
    
    errlog_init();

    array_init(&as->symbol_table, sizeof(DYNARRAY));
    array_resize(&as->symbol_table, 256);
    for(int i=0; i < 256; i++) {
        array_init(ARRAY_GET(&as->symbol_table, DYNARRAY, i), sizeof(SYMBOL_LABEL));
    }
    array_init(&as->anon_symbols, sizeof(uint16_t));
    array_init(&as->loop_stack, sizeof(FOR_LOOP));
    
    include_files_init();

    as->start_address = -1;
    as->last_address = 0;
    as->pass = 0;
    return A2_OK;
}

void assembler_shutdown() {
    include_files_cleanup();
    errlog_shutdown(errorlog.log_array);
    array_free(&as->anon_symbols);
    for(int i=0; i < 256; i++) {
        array_free(ARRAY_GET(&as->symbol_table, DYNARRAY, i));
    }
    array_free(&as->symbol_table);
    free(as->strcode_work);
    free(as->strcode);
}

