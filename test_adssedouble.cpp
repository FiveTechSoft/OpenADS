// Test program for AdsSetDouble functionality
// Compile with: cl /EHsc test_adssedouble.cpp /I"C:\OpenADS\include" /link "C:\OpenADS\build\msvc-x64\src\Release\openace64.lib" "C:\OpenADS\build\msvc-x64\openads_core.lib"

#include <stdio.h>
#include <string.h>
#include "openads/ads.h"

#define TEST_DBF_PATH "C:\\Temp\\test_double.dbf"

// Helper to create a simple DBF with a double field
int create_test_dbf() {
    FILE* f = fopen(TEST_DBF_PATH, "wb");
    if (!f) return -1;
    
    // DBF header (simplified)
    unsigned char header[32] = {0};
    header[0] = 0x03;  // DBF type (dBase III)
    header[1] = 0x48;  // Year (2024)
    header[2] = 0x07;  // Month
    header[3] = 0x15;  // Day
    
    // Write header
    fwrite(header, 1, 32, f);
    
    // Field descriptor: NAME (C, 20)
    unsigned char field[32] = {0};
    strcpy((char*)field, "NAME");
    field[11] = 'C';  // Type
    *(unsigned long*)&field[12] = 20;  // Length
    fwrite(field, 1, 32, f);
    
    // Field descriptor: MYDOUBLE (N, 10, 2)
    memset(field, 0, 32);
    strcpy((char*)field, "MYDOUBLE");
    field[11] = 'N';  // Numeric
    *(unsigned long*)&field[12] = 10;  // Length
    field[16] = 2;   // Decimals
    fwrite(field, 1, 32, f);
    
    // Header terminator
    fputc(0x0D, f);
    
    // Add some records
    unsigned char record[128] = {0};
    record[0] = ' ';  // Not deleted
    
    // Record 1
    strncpy((char*)(record + 1), "AAA", 20);
    strncpy((char*)(record + 21), "123.45", 10);
    fwrite(record, 1, sizeof(record), f);
    
    // Record 2
    memset(record, 0, sizeof(record));
    record[0] = ' ';
    strncpy((char*)(record + 1), "BBB", 20);
    strncpy((char*)(record + 21), "678.90", 10);
    fwrite(record, 1, sizeof(record), f);
    
    fclose(f);
    return 0;
}

void print_error(const char* prefix, UNSIGNED32 code) {
    printf("%s Error %d\n", prefix, code);
}

int main() {
    ADSHANDLE hConnect = 0;
    ADSHANDLE hTable = 0;
    UNSIGNED32 rc;
    
    printf("=== Testing AdsSetDouble ===\n\n");
    
    // Create test DBF
    printf("Creating test DBF...\n");
    if (create_test_dbf() != 0) {
        printf("Failed to create test DBF\n");
        return 1;
    }
    
    // Connect
    printf("Connecting...\n");
    UNSIGNED8 conn_str[256];
    strcpy((char*)conn_str, "C:\\Temp\\");
    rc = AdsConnect60(conn_str, ADS_LOCAL_SERVER, NULL, NULL, 0, &hConnect);
    if (rc != AE_SUCCESS) {
        print_error("AdsConnect60", rc);
        return 1;
    }
    printf("Connected successfully\n\n");
    
    // Open table
    printf("Opening table...\n");
    UNSIGNED8 table_name[] = "test_double.dbf";
    rc = AdsOpenTable(hConnect, table_name, NULL, ADS_CDX, 0, 0, 0, 0, &hTable);
    if (rc != AE_SUCCESS) {
        print_error("AdsOpenTable", rc);
        AdsDisconnect(hConnect);
        return 1;
    }
    printf("Table opened successfully\n\n");
    
    // Test 1: SetDouble by field name
    printf("Test 1: AdsSetDouble by field name\n");
    rc = AdsGotoTop(hTable);
    if (rc != AE_SUCCESS) {
        print_error("AdsGotoTop", rc);
        AdsCloseTable(hTable);
        AdsDisconnect(hConnect);
        return 1;
    }
    
    UNSIGNED8 field_name[] = "MYDOUBLE";
    double test_value = 999.99;
    rc = AdsSetDouble(hTable, field_name, test_value);
    if (rc != AE_SUCCESS) {
        print_error("AdsSetDouble (by name)", rc);
    } else {
        printf("  SUCCESS: Set MYDOUBLE = %.2f\n", test_value);
        
        // Verify
        UNSIGNED8 buf[64];
        UNSIGNED16 cap = sizeof(buf);
        rc = AdsGetField(hTable, field_name, buf, &cap, 0);
        if (rc == AE_SUCCESS) {
            printf("  Verified: Got value '%s' (len=%d)\n", buf, cap);
        }
    }
    printf("\n");
    
    // Test 2: SetDouble by ordinal
    printf("Test 2: AdsSetDouble by ordinal\n");
    rc = AdsGotoTop(hTable);
    if (rc == AE_SUCCESS) {
        UNSIGNED8 ordinal[] = {0x02};  // Field index 2 (0-based)
        double test_value2 = 555.55;
        rc = AdsSetDouble(hTable, ordinal, test_value2);
        if (rc != AE_SUCCESS) {
            print_error("AdsSetDouble (by ordinal)", rc);
        } else {
            printf("  SUCCESS: Set field 2 = %.2f\n", test_value2);
            
            // Verify
            UNSIGNED8 buf[64];
            UNSIGNED16 cap = sizeof(buf);
            rc = AdsGetField(hTable, ordinal, buf, &cap, 0);
            if (rc == AE_SUCCESS) {
                printf("  Verified: Got value '%s' (len=%d)\n", buf, cap);
            }
        }
    }
    printf("\n");
    
    // Test 3: Multiple SetDouble calls
    printf("Test 3: Multiple AdsSetDouble calls\n");
    for (int i = 1; i <= 3; i++) {
        rc = AdsGotoTop(hTable);
        if (rc == AE_SUCCESS) {
            double val = i * 100.0;
            rc = AdsSetDouble(hTable, field_name, val);
            if (rc == AE_SUCCESS) {
                printf("  Record %d: Set value = %.2f\n", i, val);
                
                // Verify
                UNSIGNED8 buf[64];
                UNSIGNED16 cap = sizeof(buf);
                rc = AdsGetField(hTable, field_name, buf, &cap, 0);
                if (rc == AE_SUCCESS) {
                    printf("    Verified: '%s'\n", buf);
                }
            } else {
                print_error("AdsSetDouble (iteration)", rc);
            }
        }
    }
    printf("\n");
    
    // Test 4: Extreme values
    printf("Test 4: Extreme values\n");
    double extreme_values[] = {0.0, 1e10, 1e-10, -123.456, 999999.99};
    for (int i = 0; i < 5; i++) {
        rc = AdsGotoTop(hTable);
        if (rc == AE_SUCCESS) {
            rc = AdsSetDouble(hTable, field_name, extreme_values[i]);
            if (rc == AE_SUCCESS) {
                printf("  Set extreme value: %.10g\n", extreme_values[i]);
            } else {
                print_error("AdsSetDouble (extreme)", rc);
            }
        }
    }
    printf("\n");
    
    // Cleanup
    printf("Cleaning up...\n");
    AdsCloseTable(hTable);
    AdsDisconnect(hConnect);
    
    printf("\n=== Tests completed ===\n");
    return 0;
}
