/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

#include "../test-util.h"

#if defined(__AROS__)
#include <CUnit/CUnitCI.h>
#endif

/* Helper function to create a test file in RAM: */
static VOID create_test_file(CONST_STRPTR filename)
{
    BPTR fh;

    fh = Open(filename, MODE_NEWFILE);
    if (fh)
    {
        Write(fh, "test", 4);
        Close(fh);
    }
}

/* Helper function to check if a file exists */
static BOOL file_exists(CONST_STRPTR filename)
{
    BPTR lock = Lock(filename, SHARED_LOCK);

    if (lock)
    {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
}

/* Helper function to delete a file if it exists */
static void delete_file_if_exists(CONST_STRPTR filename)
{
    if (file_exists(filename))
    {
        DeleteFile(filename);
    }
}

/*
 * Test basic rename with different names works
 */
void test_rename_different_names(void)
{
    LONG result;

    /* Clean up any previous test files */
    delete_file_if_exists("RAM:testfile_old");
    delete_file_if_exists("RAM:testfile_new");

    /* Create test file */
    create_test_file("RAM:testfile_old");
    CU_ASSERT(file_exists("RAM:testfile_old"));

    /* Rename to different name should succeed */
    result = Rename("RAM:testfile_old", "RAM:testfile_new");
    CU_ASSERT_EQUAL(result, DOSTRUE);

    /* Old name should no longer exist */
    CU_ASSERT(!file_exists("RAM:testfile_old"));

    /* New name should exist */
    CU_ASSERT(file_exists("RAM:testfile_new"));

    /* Clean up */
    delete_file_if_exists("RAM:testfile_new");
}

/*
 * Test rename with mixed case change in multi-character filename
 */
void test_rename_case_change_mixed_RAM(void)
{
    LONG result;

    /* Clean up any previous test files */
    delete_file_if_exists("RAM:MyTestFile");
    delete_file_if_exists("RAM:MYTESTFILE");

    /* Create test file */
    create_test_file("RAM:MyTestFile");
    CU_ASSERT(file_exists("RAM:MyTestFile"));

    /* Try to rename to all uppercase */
    result = Rename("RAM:MyTestFile", "RAM:MYTESTFILE");
    CU_ASSERT_EQUAL(result, DOSTRUE);

    /* New name should exist */
    CU_ASSERT(file_exists("RAM:MYTESTFILE"));

    /* Clean up */
    delete_file_if_exists("RAM:MYTESTFILE");
}

/*
 * Test rename with mixed case change in multi-character filename
 */
void test_rename_case_change_mixed_SYS(void)
{
    LONG result;

    /* Clean up any previous test files */
    delete_file_if_exists("SYS:MyTestFile");
    delete_file_if_exists("SYS:MYTESTFILE");

    /* Create test file */
    create_test_file("SYS:MyTestFile");
    CU_ASSERT(file_exists("SYS:MyTestFile"));

    /* Try to rename to all uppercase */
    result = Rename("SYS:MyTestFile", "SYS:MYTESTFILE");
    CU_ASSERT_EQUAL(result, DOSTRUE);

    /* New name should exist */
    CU_ASSERT(file_exists("SYS:MYTESTFILE"));

    /* Clean up */
    delete_file_if_exists("SYS:MYTESTFILE");
}

/*
 * Test that rename to existing file with different name still fails
 */
void test_rename_to_existing_file_fails(void)
{
    LONG result;

    /* Clean up */
    delete_file_if_exists("RAM:file1");
    delete_file_if_exists("RAM:file2");

    /* Create two files */
    create_test_file("RAM:file1");
    create_test_file("RAM:file2");

    CU_ASSERT(file_exists("RAM:file1"));
    CU_ASSERT(file_exists("RAM:file2"));

    /* Try to rename file1 to file2 (which exists) - should fail */
    result = Rename("RAM:file1", "RAM:file2");

    /* This should fail with ERROR_OBJECT_EXISTS */
    CU_ASSERT_EQUAL(result, DOSFALSE);
    CU_ASSERT_EQUAL(IoErr(), ERROR_OBJECT_EXISTS);

    /* Both files should still exist */
    CU_ASSERT(file_exists("RAM:file1"));
    CU_ASSERT(file_exists("RAM:file2"));

    /* Clean up */
    delete_file_if_exists("RAM:file1");
    delete_file_if_exists("RAM:file2");
}

/*
 * Test that renaming non-existent file fails appropriately
 */
void test_rename_nonexistent_file_fails(void)
{
    LONG result;

    /* Clean up */
    delete_file_if_exists("RAM:nonexistent_source");
    delete_file_if_exists("RAM:nonexistent_dest");

    /* Try to rename a file that doesn't exist */
    result = Rename("RAM:nonexistent_source", "RAM:nonexistent_dest");

    /* Should fail with ERROR_OBJECT_NOT_FOUND */
    CU_ASSERT_EQUAL(result, DOSFALSE);
    CU_ASSERT_EQUAL(IoErr(), ERROR_OBJECT_NOT_FOUND);
}

int main(int argc, char** argv)
{
    CU_CI_DEFINE_SUITE(Rename_Suite, NULL, NULL, NULL, NULL);
    CUNIT_CI_TEST(test_rename_different_names);
    CUNIT_CI_TEST(test_rename_case_change_mixed_RAM);
    CUNIT_CI_TEST(test_rename_case_change_mixed_SYS);
    CUNIT_CI_TEST(test_rename_to_existing_file_fails);
    CUNIT_CI_TEST(test_rename_nonexistent_file_fails);
    return CU_CI_RUN_SUITES();
}
