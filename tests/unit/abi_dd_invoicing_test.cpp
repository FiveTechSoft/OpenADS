// abi_dd_invoicing_test.cpp
//
// Comprehensive DD creation test -- a complete invoicing system DD built from
// scratch using the C++ DataDict API on top of ABI-created ADT/ADI tables.
//
// Schema:
//   Tables (ADT/ADI): customers, products, invoices, inv_detail, payments
//   Functions (3):    fn_discount, fn_tax, fn_outstanding
//   Stored procs (2): sp_create_invoice, sp_apply_payment
//   Triggers (4):     insert-audit on invoices, delete-restrict on invoices,
//                     total-update on inv_detail, status-update on payments
//   RI rules (3):     fk_inv_cust, fk_detail_inv, fk_pay_inv
//   Groups (3):       Managers, Clerks, Auditors
//   Users (4):        admin (DB:Admin+Managers), manager1, clerk1, auditor1
//   Permissions:      Managers=full, Clerks=read+write, Auditors=read-only;
//                     EXECUTE grants on SPs and functions per group
//
// Bugs covered:
//   - FUNC entries not written/read in text-format .add  (fixed in data_dict.cpp)
//   - Non-Table PERM entries not written/read in text-format .add (fixed too)

#include "doctest.h"
#include "engine/data_dict.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::DataDict;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Connect to a bare directory (no DD) for table/index creation.
ADSHANDLE dir_connect(const fs::path& dir) {
    auto s = dir.string();
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, s.c_str(), s.size());
    ADSHANDLE h = 0;
    AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h);
    return h;
}

// Create an ADT table in hConn's directory.  Returns the open table handle.
ADSHANDLE make_adt(ADSHANDLE hConn, const char* filename, const char* flddef) {
    ADSHANDLE hTable = 0;
    std::vector<UNSIGNED8> fbuf(std::strlen(filename) + 1);
    std::memcpy(fbuf.data(), filename, fbuf.size());
    std::vector<UNSIGNED8> dbuf(std::strlen(flddef) + 1);
    std::memcpy(dbuf.data(), flddef, dbuf.size());
    AdsCreateTable(hConn, fbuf.data(), nullptr, ADS_ADT, ADS_ANSI,
                   0, 0, 0, dbuf.data(), &hTable);
    return hTable;
}

// Add an ADI tag to an existing open ADT table.
// The .adi bag file is created on first call and appended on subsequent calls.
void add_adi_tag(ADSHANDLE hTable, const char* adi_file,
                 const char* tag, const char* expr) {
    ADSHANDLE hIdx = 0;
    std::vector<UNSIGNED8> fbuf(std::strlen(adi_file) + 1);
    std::memcpy(fbuf.data(), adi_file, fbuf.size());
    std::vector<UNSIGNED8> tbuf(std::strlen(tag) + 1);
    std::memcpy(tbuf.data(), tag, tbuf.size());
    std::vector<UNSIGNED8> ebuf(std::strlen(expr) + 1);
    std::memcpy(ebuf.data(), expr, ebuf.size());
    AdsCreateIndex61(hTable, fbuf.data(), tbuf.data(), ebuf.data(),
                     nullptr, nullptr, 0, 0, &hIdx);
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct InvoicingFixture {
    fs::path dir;
    fs::path add_path;
    bool     ok = false;

    InvoicingFixture() {
        dir      = fs::temp_directory_path() / "openads_dd_invoicing";
        add_path = dir / "invoicing.add";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ok = build();
    }

    ~InvoicingFixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Open the DD fresh from disk (verifies persistence).
    DataDict reopen() const {
        auto r = DataDict::open(add_path.string());
        REQUIRE(r.has_value());
        return std::move(r).value();
    }

private:
    bool build() {
        if (!create_adt_tables()) return false;

        // Build the Data Dictionary via the C++ API.
        auto cr = DataDict::create(add_path.string());
        if (!cr.has_value()) return false;
        DataDict dd = std::move(cr).value();

        // â”€â”€ Tables â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!dd.add_table("customers",  "./customers.adt").has_value())  return false;
        if (!dd.add_table("products",   "./products.adt").has_value())   return false;
        if (!dd.add_table("invoices",   "./invoices.adt").has_value())   return false;
        if (!dd.add_table("inv_detail", "./inv_detail.adt").has_value()) return false;
        if (!dd.add_table("payments",   "./payments.adt").has_value())   return false;

        // â”€â”€ Index files â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!dd.add_index_file("customers",  "./customers.adi",  "").has_value()) return false;
        if (!dd.add_index_file("products",   "./products.adi",   "").has_value()) return false;
        if (!dd.add_index_file("invoices",   "./invoices.adi",   "").has_value()) return false;
        if (!dd.add_index_file("inv_detail", "./inv_detail.adi", "").has_value()) return false;
        if (!dd.add_index_file("payments",   "./payments.adi",   "").has_value()) return false;

        // â”€â”€ User-defined functions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            DataDict::FunctionEntry fe;
            fe.name           = "fn_discount";
            fe.input_params   = "qty:N";
            fe.return_type    = "N";
            fe.comment        = "Volume discount rate by quantity tier";
            fe.implementation = "SELECT CASE WHEN :qty>=100 THEN 0.15 "
                                "WHEN :qty>=50 THEN 0.10 ELSE 0.05 END";
            if (!dd.create_function(fe).has_value()) return false;
        }
        {
            DataDict::FunctionEntry fe;
            fe.name           = "fn_tax";
            fe.input_params   = "amount:N";
            fe.return_type    = "N";
            fe.comment        = "8 percent sales tax";
            fe.implementation = "SELECT :amount * 0.08";
            if (!dd.create_function(fe).has_value()) return false;
        }
        {
            DataDict::FunctionEntry fe;
            fe.name           = "fn_outstanding";
            fe.input_params   = "inv_id:C";
            fe.return_type    = "N";
            fe.comment        = "Outstanding balance for invoice";
            fe.implementation = "SELECT i.TotalAmt - COALESCE(SUM(p.Amount),0) "
                                "FROM invoices i LEFT JOIN payments p "
                                "ON p.InvID=i.InvID WHERE i.InvID=:inv_id";
            if (!dd.create_function(fe).has_value()) return false;
        }

        // â”€â”€ Stored procedures â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            DataDict::ProcEntry pe;
            pe.name          = "sp_create_invoice";
            pe.container     = "invoicing.dll";
            pe.procedure     = "CreateInvoice";
            pe.input_params  = "cust_id:C;inv_date:D";
            pe.output_params = "inv_id:C;result:N";
            pe.comment       = "Create new invoice header";
            if (!dd.create_proc(pe).has_value()) return false;
        }
        {
            DataDict::ProcEntry pe;
            pe.name          = "sp_apply_payment";
            pe.container     = "invoicing.dll";
            pe.procedure     = "ApplyPayment";
            pe.input_params  = "inv_id:C;amount:N;method:C";
            pe.output_params = "pay_id:C;balance:N";
            pe.comment       = "Apply payment and return new balance";
            if (!dd.create_proc(pe).has_value()) return false;
        }

        // â”€â”€ Triggers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            DataDict::TriggerEntry te;
            te.name        = "trg_inv_insert";
            te.table_alias = "invoices";
            te.event_mask  = 1;  // INSERT
            te.timing      = 4;  // AFTER
            te.container   = "invoicing.dll";
            te.procedure   = "AuditInvoiceInsert";
            te.priority    = 10;
            te.comment     = "Log invoice creation to audit table";
            if (!dd.create_trigger(te).has_value()) return false;
        }
        {
            DataDict::TriggerEntry te;
            te.name        = "trg_inv_delete";
            te.table_alias = "invoices";
            te.event_mask  = 4;  // DELETE
            te.timing      = 1;  // BEFORE
            te.container   = "invoicing.dll";
            te.procedure   = "RestrictInvoiceDelete";
            te.priority    = 5;
            te.comment     = "Block delete when payments exist";
            if (!dd.create_trigger(te).has_value()) return false;
        }
        {
            DataDict::TriggerEntry te;
            te.name        = "trg_detail_insert";
            te.table_alias = "inv_detail";
            te.event_mask  = 1;  // INSERT
            te.timing      = 4;  // AFTER
            te.container   = "invoicing.dll";
            te.procedure   = "UpdateInvoiceTotal";
            te.priority    = 10;
            te.comment     = "Recalculate TotalAmt after line insert";
            if (!dd.create_trigger(te).has_value()) return false;
        }
        {
            DataDict::TriggerEntry te;
            te.name        = "trg_pay_insert";
            te.table_alias = "payments";
            te.event_mask  = 1;  // INSERT
            te.timing      = 4;  // AFTER
            te.container   = "invoicing.dll";
            te.procedure   = "UpdateInvoiceStatus";
            te.priority    = 10;
            te.comment     = "Mark invoice Paid when balance reaches zero";
            if (!dd.create_trigger(te).has_value()) return false;
        }

        // â”€â”€ Referential integrity â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            DataDict::RiEntry re;
            re.name       = "fk_inv_cust";
            re.parent     = "customers";
            re.child      = "invoices";
            re.parent_tag = "CUSTID";
            re.child_tag  = "INVCUST";
            re.update_opt = "1";  // RESTRICT
            re.delete_opt = "1";
            if (!dd.create_ri(re).has_value()) return false;
        }
        {
            DataDict::RiEntry re;
            re.name       = "fk_detail_inv";
            re.parent     = "invoices";
            re.child      = "inv_detail";
            re.parent_tag = "INVID";
            re.child_tag  = "DETINVID";
            re.update_opt = "2";  // CASCADE
            re.delete_opt = "2";
            if (!dd.create_ri(re).has_value()) return false;
        }
        {
            DataDict::RiEntry re;
            re.name       = "fk_pay_inv";
            re.parent     = "invoices";
            re.child      = "payments";
            re.parent_tag = "INVID";
            re.child_tag  = "PAYINVID";
            re.update_opt = "1";  // RESTRICT
            re.delete_opt = "1";
            if (!dd.create_ri(re).has_value()) return false;
        }

        // â”€â”€ Groups â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!dd.create_group("Managers").has_value())  return false;
        if (!dd.create_group("Clerks").has_value())    return false;
        if (!dd.create_group("Auditors").has_value())  return false;

        // â”€â”€ Users â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // admin â†’ DB:Admin + Managers (default admin with elevated rights)
        if (!dd.create_user("admin").has_value()) return false;
        if (!dd.set_user_property("admin", "prop_1101", "admin123").has_value()) return false;
        if (!dd.add_user_to_group("admin", "DB:Admin").has_value())  return false;
        if (!dd.add_user_to_group("admin", "Managers").has_value())  return false;

        // manager1 â†’ Managers
        if (!dd.create_user("manager1").has_value()) return false;
        if (!dd.set_user_property("manager1", "prop_1101", "mgr111").has_value()) return false;
        if (!dd.add_user_to_group("manager1", "Managers").has_value()) return false;

        // clerk1 â†’ Clerks
        if (!dd.create_user("clerk1").has_value()) return false;
        if (!dd.set_user_property("clerk1", "prop_1101", "clk111").has_value()) return false;
        if (!dd.add_user_to_group("clerk1", "Clerks").has_value()) return false;

        // auditor1 â†’ Auditors
        if (!dd.create_user("auditor1").has_value()) return false;
        if (!dd.set_user_property("auditor1", "prop_1101", "aud111").has_value()) return false;
        if (!dd.add_user_to_group("auditor1", "Auditors").has_value()) return false;

        // â”€â”€ Table permissions (coarse-level, persisted via TABLEPERM) â”€â”€â”€â”€â”€â”€â”€â”€â”€
        static const char* kAllTables[] = {
            "customers", "products", "invoices", "inv_detail", "payments"
        };
        for (const char* tbl : kAllTables) {
            if (!dd.set_table_permission(tbl, "Managers", 4).has_value()) return false;
            if (!dd.set_table_permission(tbl, "Auditors", 1).has_value()) return false;
        }
        // Clerks: read+write on transactional tables, read-only on products
        static const char* kClerkWrite[] = {
            "customers", "invoices", "inv_detail", "payments"
        };
        for (const char* tbl : kClerkWrite) {
            if (!dd.set_table_permission(tbl, "Clerks", 2).has_value()) return false;
        }
        if (!dd.set_table_permission("products", "Clerks", 1).has_value()) return false;

        // â”€â”€ Fine-grained permissions (EXECUTE on SPs and functions) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Managers and Clerks can execute SPs; Auditors cannot.
        static const char* kSPs[] = {"sp_create_invoice", "sp_apply_payment"};
        for (const char* sp : kSPs) {
            if (!dd.grant_permission("StoredProc", sp, "Managers", 0x004u).has_value()) return false;
            if (!dd.grant_permission("StoredProc", sp, "Clerks",   0x004u).has_value()) return false;
        }
        // All groups can call functions.
        static const char* kFns[] = {"fn_discount", "fn_tax", "fn_outstanding"};
        for (const char* fn : kFns) {
            if (!dd.grant_permission("Function", fn, "Managers",  0x004u).has_value()) return false;
            if (!dd.grant_permission("Function", fn, "Clerks",    0x004u).has_value()) return false;
            if (!dd.grant_permission("Function", fn, "Auditors",  0x004u).has_value()) return false;
        }

        // â”€â”€ DB properties â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        if (!dd.set_db_property("prop_5", "1").has_value()) return false;  // login required

        return true;
    }

    // Create the 5 ADT tables and their ADI indexes.
    bool create_adt_tables() {
        ADSHANDLE hConn = dir_connect(dir);
        if (!hConn) return false;

        bool all_ok = create_customers(hConn)
                   && create_products(hConn)
                   && create_invoices(hConn)
                   && create_inv_detail(hConn)
                   && create_payments(hConn);

        AdsDisconnect(hConn);
        return all_ok;
    }

    bool create_customers(ADSHANDLE hConn) {
        ADSHANDLE h = make_adt(hConn, "customers.adt",
            "CustID,Character,10;"
            "CustName,Character,60;"
            "Email,Character,80;"
            "Phone,Character,20;"
            "Active,Logical");
        if (!h) return false;
        add_adi_tag(h, "customers.adi", "CUSTID",   "CustID");
        add_adi_tag(h, "customers.adi", "CUSTNAME", "CustName");
        AdsCloseTable(h);
        return true;
    }

    bool create_products(ADSHANDLE hConn) {
        ADSHANDLE h = make_adt(hConn, "products.adt",
            "ProdID,Character,10;"
            "Descr,Character,80;"
            "UnitPrice,Numeric,12,2;"
            "StockQty,Numeric,8;"
            "Active,Logical");
        if (!h) return false;
        add_adi_tag(h, "products.adi", "PRODID",    "ProdID");
        add_adi_tag(h, "products.adi", "PRODDESCR", "Descr");
        AdsCloseTable(h);
        return true;
    }

    bool create_invoices(ADSHANDLE hConn) {
        ADSHANDLE h = make_adt(hConn, "invoices.adt",
            "InvID,Character,12;"
            "CustID,Character,10;"
            "InvDate,Date;"
            "TotalAmt,Numeric,12,2;"
            "Status,Character,10");
        if (!h) return false;
        add_adi_tag(h, "invoices.adi", "INVID",   "InvID");
        add_adi_tag(h, "invoices.adi", "INVCUST", "CustID");
        add_adi_tag(h, "invoices.adi", "INVDATE", "InvDate");
        AdsCloseTable(h);
        return true;
    }

    bool create_inv_detail(ADSHANDLE hConn) {
        ADSHANDLE h = make_adt(hConn, "inv_detail.adt",
            "DetailID,Character,14;"
            "InvID,Character,12;"
            "ProdID,Character,10;"
            "Qty,Numeric,8;"
            "UnitPrice,Numeric,12,2;"
            "LineTotal,Numeric,12,2");
        if (!h) return false;
        add_adi_tag(h, "inv_detail.adi", "DETAILID", "DetailID");
        add_adi_tag(h, "inv_detail.adi", "DETINVID", "InvID");
        add_adi_tag(h, "inv_detail.adi", "DETPROD",  "ProdID");
        AdsCloseTable(h);
        return true;
    }

    bool create_payments(ADSHANDLE hConn) {
        ADSHANDLE h = make_adt(hConn, "payments.adt",
            "PayID,Character,12;"
            "InvID,Character,12;"
            "PayDate,Date;"
            "Amount,Numeric,12,2;"
            "Method,Character,20");
        if (!h) return false;
        add_adi_tag(h, "payments.adi", "PAYID",    "PayID");
        add_adi_tag(h, "payments.adi", "PAYINVID", "InvID");
        add_adi_tag(h, "payments.adi", "PAYDATE",  "PayDate");
        AdsCloseTable(h);
        return true;
    }
};

}  // namespace

// ===========================================================================
// Test cases -- each reopens the DD from disk to verify round-trip persistence
// ===========================================================================

TEST_CASE("DD invoicing -- ADT and ADI files created on disk") {
    InvoicingFixture f;
    REQUIRE(f.ok);

    // All ADT files must exist.
    for (const char* name : {"customers.adt", "products.adt", "invoices.adt",
                              "inv_detail.adt", "payments.adt"}) {
        CHECK(fs::exists(f.dir / name));
    }
    // All ADI files must exist.
    for (const char* name : {"customers.adi", "products.adi", "invoices.adi",
                              "inv_detail.adi", "payments.adi"}) {
        CHECK(fs::exists(f.dir / name));
    }
    // DD file itself.
    CHECK(fs::exists(f.add_path));
}

TEST_CASE("DD invoicing -- tables registered in DD") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    CHECK(dd.has_alias("customers"));
    CHECK(dd.has_alias("products"));
    CHECK(dd.has_alias("invoices"));
    CHECK(dd.has_alias("inv_detail"));
    CHECK(dd.has_alias("payments"));

    CHECK(dd.resolve("customers")  == "./customers.adt");
    CHECK(dd.resolve("invoices")   == "./invoices.adt");
    CHECK(dd.resolve("inv_detail") == "./inv_detail.adt");
    CHECK(dd.resolve("payments")   == "./payments.adt");
}

TEST_CASE("DD invoicing -- index files registered in DD") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    // Each table must have exactly one index file entry.
    auto has_index = [&](const char* alias, const char* idx_path) {
        for (const auto& ie : dd.indexes()) {
            if (ie.table_alias == alias && ie.index_path == idx_path) return true;
        }
        return false;
    };
    CHECK(has_index("customers",  "./customers.adi"));
    CHECK(has_index("products",   "./products.adi"));
    CHECK(has_index("invoices",   "./invoices.adi"));
    CHECK(has_index("inv_detail", "./inv_detail.adi"));
    CHECK(has_index("payments",   "./payments.adi"));
}

TEST_CASE("DD invoicing -- functions round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    REQUIRE(dd.has_function("fn_discount"));
    REQUIRE(dd.has_function("fn_tax"));
    REQUIRE(dd.has_function("fn_outstanding"));

    const auto& disc = dd.functions().at("fn_discount");
    CHECK(disc.input_params == "qty:N");
    CHECK(disc.return_type  == "N");
    CHECK(disc.implementation.find("0.15") != std::string::npos);

    const auto& tax = dd.functions().at("fn_tax");
    CHECK(tax.input_params   == "amount:N");
    CHECK(tax.implementation.find("0.08") != std::string::npos);

    const auto& out = dd.functions().at("fn_outstanding");
    CHECK(out.input_params == "inv_id:C");
    CHECK(out.implementation.find("payments") != std::string::npos);
}

TEST_CASE("DD invoicing -- stored procedures round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    REQUIRE(dd.has_proc("sp_create_invoice"));
    REQUIRE(dd.has_proc("sp_apply_payment"));

    const auto& ci = dd.procs().at("sp_create_invoice");
    CHECK(ci.container     == "invoicing.dll");
    CHECK(ci.procedure     == "CreateInvoice");
    CHECK(ci.input_params  == "cust_id:C;inv_date:D");
    CHECK(ci.output_params == "inv_id:C;result:N");

    const auto& ap = dd.procs().at("sp_apply_payment");
    CHECK(ap.container     == "invoicing.dll");
    CHECK(ap.procedure     == "ApplyPayment");
    CHECK(ap.input_params  == "inv_id:C;amount:N;method:C");
    CHECK(ap.output_params == "pay_id:C;balance:N");
}

TEST_CASE("DD invoicing -- triggers round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    // Lookup by composite "table::name" key.
    const DataDict::TriggerEntry* ti = dd.find_trigger("invoices::trg_inv_insert");
    REQUIRE(ti != nullptr);
    CHECK(ti->table_alias == "invoices");
    CHECK(ti->event_mask  == 1u);   // INSERT
    CHECK(ti->timing      == 4u);   // AFTER
    CHECK(ti->container   == "invoicing.dll");
    CHECK(ti->procedure   == "AuditInvoiceInsert");
    CHECK(ti->priority    == 10u);

    const DataDict::TriggerEntry* td = dd.find_trigger("invoices::trg_inv_delete");
    REQUIRE(td != nullptr);
    CHECK(td->event_mask == 4u);   // DELETE
    CHECK(td->timing     == 1u);   // BEFORE
    CHECK(td->procedure  == "RestrictInvoiceDelete");

    const DataDict::TriggerEntry* tdi = dd.find_trigger("inv_detail::trg_detail_insert");
    REQUIRE(tdi != nullptr);
    CHECK(tdi->table_alias == "inv_detail");
    CHECK(tdi->procedure   == "UpdateInvoiceTotal");

    const DataDict::TriggerEntry* tp = dd.find_trigger("payments::trg_pay_insert");
    REQUIRE(tp != nullptr);
    CHECK(tp->table_alias == "payments");
    CHECK(tp->procedure   == "UpdateInvoiceStatus");
}

TEST_CASE("DD invoicing -- referential integrity rules round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    REQUIRE(dd.ri().count("fk_inv_cust")   == 1u);
    REQUIRE(dd.ri().count("fk_detail_inv") == 1u);
    REQUIRE(dd.ri().count("fk_pay_inv")    == 1u);

    const auto& r1 = dd.ri().at("fk_inv_cust");
    CHECK(r1.parent     == "customers");
    CHECK(r1.child      == "invoices");
    CHECK(r1.parent_tag == "CUSTID");
    CHECK(r1.child_tag  == "INVCUST");
    CHECK(r1.update_opt == "1");
    CHECK(r1.delete_opt == "1");

    const auto& r2 = dd.ri().at("fk_detail_inv");
    CHECK(r2.parent     == "invoices");
    CHECK(r2.child      == "inv_detail");
    CHECK(r2.update_opt == "2");  // CASCADE
    CHECK(r2.delete_opt == "2");

    const auto& r3 = dd.ri().at("fk_pay_inv");
    CHECK(r3.parent == "invoices");
    CHECK(r3.child  == "payments");
}

TEST_CASE("DD invoicing -- groups, users, memberships round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    // Groups
    CHECK(dd.has_group("Managers"));
    CHECK(dd.has_group("Clerks"));
    CHECK(dd.has_group("Auditors"));

    // Users
    CHECK(dd.has_user("admin"));
    CHECK(dd.has_user("manager1"));
    CHECK(dd.has_user("clerk1"));
    CHECK(dd.has_user("auditor1"));

    // Memberships
    CHECK(dd.is_member_of("admin",    "DB:Admin"));
    CHECK(dd.is_member_of("admin",    "Managers"));
    CHECK(dd.is_member_of("manager1", "Managers"));
    CHECK(dd.is_member_of("clerk1",   "Clerks"));
    CHECK(dd.is_member_of("auditor1", "Auditors"));

    // Negative checks -- no cross-group leakage
    CHECK_FALSE(dd.is_member_of("clerk1",   "Managers"));
    CHECK_FALSE(dd.is_member_of("auditor1", "Clerks"));
    CHECK_FALSE(dd.is_member_of("clerk1",   "DB:Admin"));
}

TEST_CASE("DD invoicing -- table permissions round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    // Managers: full (level 4) on all tables
    for (const char* tbl : {"customers", "products", "invoices",
                              "inv_detail", "payments"}) {
        CHECK(dd.get_effective_permission("manager1", tbl) == 4);
    }

    // Auditors: read-only (level 1) everywhere
    for (const char* tbl : {"customers", "products", "invoices",
                              "inv_detail", "payments"}) {
        CHECK(dd.get_effective_permission("auditor1", tbl) == 1);
    }

    // Clerks: read+write (2) on transactional tables, read-only (1) on products
    for (const char* tbl : {"customers", "invoices", "inv_detail", "payments"}) {
        CHECK(dd.get_effective_permission("clerk1", tbl) == 2);
    }
    CHECK(dd.get_effective_permission("clerk1", "products") == 1);
}

TEST_CASE("DD invoicing -- SP and function execute permissions round-trip") {
    InvoicingFixture f;
    REQUIRE(f.ok);
    DataDict dd = f.reopen();

    // Helper: find a permission entry for (obj_type, obj_name, grantee).
    auto find_perm = [&](const char* obj_type, const char* obj_name,
                          const char* grantee) -> const DataDict::PermissionEntry* {
        for (const auto& pe : dd.permissions()) {
            if (pe.object_type == obj_type &&
                pe.object_name == obj_name &&
                pe.grantee     == grantee) return &pe;
        }
        return nullptr;
    };

    // Stored procedure execute grants
    for (const char* sp : {"sp_create_invoice", "sp_apply_payment"}) {
        const auto* pm = find_perm("StoredProc", sp, "Managers");
        REQUIRE(pm != nullptr);
        CHECK((pm->bitmask & 0x004u) != 0u);  // EXECUTE bit set

        const auto* pc = find_perm("StoredProc", sp, "Clerks");
        REQUIRE(pc != nullptr);
        CHECK((pc->bitmask & 0x004u) != 0u);

        // Auditors must NOT have an execute grant on SPs
        CHECK(find_perm("StoredProc", sp, "Auditors") == nullptr);
    }

    // Function execute grants -- all three groups
    for (const char* fn : {"fn_discount", "fn_tax", "fn_outstanding"}) {
        for (const char* grp : {"Managers", "Clerks", "Auditors"}) {
            const auto* pe = find_perm("Function", fn, grp);
            REQUIRE(pe != nullptr);
            CHECK((pe->bitmask & 0x004u) != 0u);
        }
    }
}

TEST_CASE("DD invoicing -- DB:Admin user can open tables via ABI") {
    InvoicingFixture f;
    REQUIRE(f.ok);

    // Connect to the DD as admin (DB:Admin member).
    auto path_str = f.add_path.string();
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, path_str.c_str(), path_str.size());
    UNSIGNED8 ubuf[16] = "admin";
    UNSIGNED8 pbuf[16] = "admin123";
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, ubuf, pbuf, 0, &hConn) == AE_SUCCESS);

    // Open each registered table.
    for (const char* alias : {"customers", "products", "invoices",
                               "inv_detail", "payments"}) {
        ADSHANDLE hTbl = 0;
        std::vector<UNSIGNED8> abuf(std::strlen(alias) + 1);
        std::memcpy(abuf.data(), alias, abuf.size());
        UNSIGNED32 rc = AdsOpenTable(hConn, abuf.data(), abuf.data(),
                                     ADS_ADT, ADS_ANSI, ADS_READONLY,
                                     ADS_COMPATIBLE_LOCKING, ADS_DEFAULT,
                                     &hTbl);
        CHECK(rc == AE_SUCCESS);
        if (hTbl) AdsCloseTable(hTbl);
    }

    AdsDisconnect(hConn);
}

