/**
 * An implementation of TPC-C based off of:
 * https://github.com/oltpbenchmark/oltpbench/tree/master/src/com/oltpbenchmark/benchmarks/tpcc
 */

#if !defined(NESTED_COROUTINE) && !defined(HYBRID_COROUTINE)

#include "tpcc-config.h"

class tpcc_sequential_worker : public bench_worker, public tpcc_worker_mixin {
 public:
  tpcc_sequential_worker(unsigned int worker_id, unsigned long seed, ermia::Engine *db,
              const std::map<std::string, ermia::OrderedIndex *> &open_tables,
              const std::map<std::string, std::vector<ermia::OrderedIndex *>> &partitions,
              spin_barrier *barrier_a, spin_barrier *barrier_b,
              uint home_warehouse_id, int, ermia::thread::Thread *me)
      : bench_worker(worker_id, true, seed, db, open_tables, barrier_a, barrier_b, me),
        tpcc_worker_mixin(partitions),
        home_warehouse_id(home_warehouse_id) {
    ASSERT(home_warehouse_id >= 1 and home_warehouse_id <= NumWarehouses() + 1);
    memset(&last_no_o_ids[0], 0, sizeof(last_no_o_ids));    
  }

  tpcc_sequential_worker(unsigned int worker_id, unsigned long seed, ermia::Engine *db,
              const std::map<std::string, ermia::OrderedIndex *> &open_tables,
              const std::map<std::string, std::vector<ermia::OrderedIndex *>> &partitions,
              spin_barrier *barrier_a, spin_barrier *barrier_b,
              uint home_warehouse_id)
      : tpcc_sequential_worker(worker_id, seed, db, open_tables, partitions, barrier_a, barrier_b, home_warehouse_id, 0, nullptr) {
    preemptive_worker = new tpcc_sequential_worker(worker_id, seed, db, open_tables, partitions, barrier_a, barrier_b, home_warehouse_id, 0, me);
  }

  // XXX(stephentu): tune this
  static const size_t NMaxCustomerIdxScanElems = 512;

  rc_t txn_new_order();

  static rc_t TxnNewOrder(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_new_order();
  }

  rc_t txn_delivery();

  static rc_t TxnDelivery(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_delivery();
  }

  rc_t txn_credit_check();
  static rc_t TxnCreditCheck(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_credit_check();
  }

  rc_t txn_payment();

  static rc_t TxnPayment(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_payment();
  }

  rc_t txn_order_status();

  static rc_t TxnOrderStatus(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_order_status();
  }

  rc_t txn_stock_level();

  static rc_t TxnStockLevel(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_stock_level();
  }

  rc_t txn_microbench_random();

  static rc_t TxnMicroBenchRandom(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_microbench_random();
  }

  rc_t txn_query2();

  static rc_t TxnQuery2(bench_worker *w) {
    return static_cast<tpcc_sequential_worker *>(w)->txn_query2();
  }

  virtual workload_desc_vec get_workload() const override;

  virtual void MyWork(char *) override;

 protected:
  ALWAYS_INLINE ermia::varstr &str(uint64_t size) { return *arena->next(size); }

 private:
  const uint home_warehouse_id;
  int32_t last_no_o_ids[10];  // XXX(stephentu): hack
};

rc_t tpcc_sequential_worker::txn_new_order() {
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint districtID = RandomNumber(r, 1, 10);
  const uint customerID = GetCustomerId(r);
  const uint numItems = RandomNumber(r, 5, 15);
  uint itemIDs[15], supplierWarehouseIDs[15], orderQuantities[15];
  bool allLocal = true;
  for (uint i = 0; i < numItems; i++) {
    itemIDs[i] = GetItemId(r);
    if (likely(FLAGS_tpcc_disable_xpartition_txn || NumWarehouses() == 1 ||
               RandomNumber(r, 1, 100) > FLAGS_tpcc_new_order_remote_item_pct)) {
      supplierWarehouseIDs[i] = warehouse_id;
    } else {
      do {
        supplierWarehouseIDs[i] = RandomNumber(r, 1, NumWarehouses());
      } while (supplierWarehouseIDs[i] == warehouse_id);
      allLocal = false;
    }
    orderQuantities[i] = RandomNumber(r, 1, 10);
  }
  ASSERT(!FLAGS_tpcc_disable_xpartition_txn || allLocal);

  // XXX(stephentu): implement rollback
  //
  // worst case txn profile:
  //   1 customer get
  //   1 warehouse get
  //   1 district get
  //   1 new_order insert
  //   1 district put
  //   1 oorder insert
  //   1 oorder_cid_idx insert
  //   15 times:
  //      1 item get
  //      1 stock get
  //      1 stock put
  //      1 order_line insert
  //
  // output from txn counters:
  //   max_absent_range_set_size : 0
  //   max_absent_set_size : 0
  //   max_node_scan_size : 0
  //   max_read_set_size : 15
  //   max_write_set_size : 15
  //   num_txn_contexts : 9
  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  const customer::key k_c(warehouse_id, districtID, customerID);
  customer::value v_c_temp;
  ermia::varstr valptr;

  rc_t rc = rc_t{RC_INVALID};
  tbl_customer(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_c)), k_c), valptr);
  TryVerifyRelaxed(rc);

  const customer::value *v_c = Decode(valptr, v_c_temp);
#ifndef NDEBUG
  checker::SanityCheckCustomer(&k_c, v_c);
#endif

  const warehouse::key k_w(warehouse_id);
  warehouse::value v_w_temp;

  rc = rc_t{RC_INVALID};
  tbl_warehouse(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_w)), k_w), valptr);
  TryVerifyRelaxed(rc);

  const warehouse::value *v_w = Decode(valptr, v_w_temp);
#ifndef NDEBUG
  checker::SanityCheckWarehouse(&k_w, v_w);
#endif

  const district::key k_d(warehouse_id, districtID);
  district::value v_d_temp;

  rc = rc_t{RC_INVALID};
  tbl_district(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_d)), k_d), valptr);
  TryVerifyRelaxed(rc);

  const district::value *v_d = Decode(valptr, v_d_temp);
#ifndef NDEBUG
  checker::SanityCheckDistrict(&k_d, v_d);
#endif

  const uint64_t my_next_o_id =
      FLAGS_tpcc_new_order_fast_id_gen ? FastNewOrderIdGen(warehouse_id, districtID)
                              : v_d->d_next_o_id;

  const new_order::key k_no(warehouse_id, districtID, my_next_o_id);
  const new_order::value v_no;
  const size_t new_order_sz = Size(v_no);
  TryCatch(tbl_new_order(warehouse_id)
                ->InsertRecord(txn, Encode(str(Size(k_no)), k_no),
                         Encode(str(new_order_sz), v_no)));

  if (!FLAGS_tpcc_new_order_fast_id_gen) {
    district::value v_d_new(*v_d);
    v_d_new.d_next_o_id++;
    TryCatch(tbl_district(warehouse_id)
                  ->UpdateRecord(txn, Encode(str(Size(k_d)), k_d),
                        Encode(str(Size(v_d_new)), v_d_new)));
  }

  const oorder::key k_oo(warehouse_id, districtID, k_no.no_o_id);
  oorder::value v_oo;
  v_oo.o_c_id = int32_t(customerID);
  v_oo.o_carrier_id = 0;  // seems to be ignored
  v_oo.o_ol_cnt = int8_t(numItems);
  v_oo.o_all_local = allLocal;
  v_oo.o_entry_d = GetCurrentTimeMillis();

  const size_t oorder_sz = Size(v_oo);
  ermia::OID v_oo_oid = 0;  // Get the OID and put it in oorder_c_id_idx later
  TryCatch(tbl_oorder(warehouse_id)
                ->InsertRecord(txn, Encode(str(Size(k_oo)), k_oo),
                         Encode(str(oorder_sz), v_oo), &v_oo_oid));

  const oorder_c_id_idx::key k_oo_idx(warehouse_id, districtID, customerID, k_no.no_o_id);
  TryCatch(tbl_oorder_c_id_idx(warehouse_id)
                ->InsertOID(txn, Encode(str(Size(k_oo_idx)), k_oo_idx), v_oo_oid));

  for (uint ol_number = 1; ol_number <= numItems; ol_number++) {
    const uint ol_supply_w_id = supplierWarehouseIDs[ol_number - 1];
    const uint ol_i_id = itemIDs[ol_number - 1];
    const uint ol_quantity = orderQuantities[ol_number - 1];

    const item::key k_i(ol_i_id);
    item::value v_i_temp;

    rc = rc_t{RC_INVALID};
    tbl_item(1)->GetRecord(txn, rc, Encode(str(Size(k_i)), k_i), valptr);
    TryVerifyRelaxed(rc);

    const item::value *v_i = Decode(valptr, v_i_temp);
#ifndef NDEBUG
    checker::SanityCheckItem(&k_i, v_i);
#endif

    const stock::key k_s(ol_supply_w_id, ol_i_id);
    stock::value v_s_temp;

    rc = rc_t{RC_INVALID};
    tbl_stock(ol_supply_w_id)->GetRecord(txn, rc, Encode(str(Size(k_s)), k_s), valptr);
    TryVerifyRelaxed(rc);

    const stock::value *v_s = Decode(valptr, v_s_temp);
#ifndef NDEBUG
    checker::SanityCheckStock(&k_s);
#endif

    stock::value v_s_new(*v_s);
    if (v_s_new.s_quantity - ol_quantity >= 10)
      v_s_new.s_quantity -= ol_quantity;
    else
      v_s_new.s_quantity += -int32_t(ol_quantity) + 91;
    v_s_new.s_ytd += ol_quantity;
    v_s_new.s_remote_cnt += (ol_supply_w_id == warehouse_id) ? 0 : 1;

    TryCatch(tbl_stock(ol_supply_w_id)
                  ->UpdateRecord(txn, Encode(str(Size(k_s)), k_s),
                        Encode(str(Size(v_s_new)), v_s_new)));

    const order_line::key k_ol(warehouse_id, districtID, k_no.no_o_id,
                               ol_number);
    order_line::value v_ol;
    v_ol.ol_i_id = int32_t(ol_i_id);
    v_ol.ol_delivery_d = 0;  // not delivered yet
    v_ol.ol_amount = float(ol_quantity) * v_i->i_price;
    v_ol.ol_supply_w_id = int32_t(ol_supply_w_id);
    v_ol.ol_quantity = int8_t(ol_quantity);

    const size_t order_line_sz = Size(v_ol);
    TryCatch(tbl_order_line(warehouse_id)
                  ->InsertRecord(txn, Encode(str(Size(k_ol)), k_ol),
                           Encode(str(order_line_sz), v_ol)));
  }

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}  // new-order

rc_t tpcc_sequential_worker::txn_payment() {
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse());
  uint customerDistrictID, customerWarehouseID;
  if (likely(FLAGS_tpcc_disable_xpartition_txn || NumWarehouses() == 1 ||
             RandomNumber(r, 1, 100) <= 85)) {
    customerDistrictID = districtID;
    customerWarehouseID = warehouse_id;
  } else {
    customerDistrictID = RandomNumber(r, 1, NumDistrictsPerWarehouse());
    do {
      customerWarehouseID = RandomNumber(r, 1, NumWarehouses());
    } while (customerWarehouseID == warehouse_id);
  }
  const float paymentAmount = (float)(RandomNumber(r, 100, 500000) / 100.0);
  const uint32_t ts = GetCurrentTimeMillis();
  ASSERT(!FLAGS_tpcc_disable_xpartition_txn || customerWarehouseID == warehouse_id);

  // output from txn counters:
  //   max_absent_range_set_size : 0
  //   max_absent_set_size : 0
  //   max_node_scan_size : 10
  //   max_read_set_size : 71
  //   max_write_set_size : 1
  //   num_txn_contexts : 5
  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);

  rc_t rc = rc_t{RC_INVALID};
  ermia::varstr valptr;
  const warehouse::key k_w(warehouse_id);
  warehouse::value v_w_temp;

  tbl_warehouse(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_w)), k_w), valptr);
  TryVerifyRelaxed(rc);

  const warehouse::value *v_w = Decode(valptr, v_w_temp);
#ifndef NDEBUG
  checker::SanityCheckWarehouse(&k_w, v_w);
#endif

  warehouse::value v_w_new(*v_w);
  v_w_new.w_ytd += paymentAmount;
  TryCatch(tbl_warehouse(warehouse_id)
                ->UpdateRecord(txn, Encode(str(Size(k_w)), k_w),
                      Encode(str(Size(v_w_new)), v_w_new)));

  const district::key k_d(warehouse_id, districtID);
  district::value v_d_temp;

  rc = rc_t{RC_INVALID};
  tbl_district(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_d)), k_d), valptr);
  TryVerifyRelaxed(rc);

  const district::value *v_d = Decode(valptr, v_d_temp);
#ifndef NDEBUG
  checker::SanityCheckDistrict(&k_d, v_d);
#endif

  district::value v_d_new(*v_d);
  v_d_new.d_ytd += paymentAmount;
  TryCatch(tbl_district(warehouse_id)
                ->UpdateRecord(txn, Encode(str(Size(k_d)), k_d),
                      Encode(str(Size(v_d_new)), v_d_new)));

  customer::key k_c;
  customer::value v_c;
  if (RandomNumber(r, 1, 100) <= 60) {
    // cust by name
    uint8_t lastname_buf[CustomerLastNameMaxSize + 1];
    static_assert(sizeof(lastname_buf) == 16, "xx");
    memset(lastname_buf, 0, sizeof(lastname_buf));
    GetNonUniformCustomerLastNameRun(lastname_buf, r);

    static const std::string zeros(16, 0);
    static const std::string ones(16, (char)255);

    customer_name_idx::key k_c_idx_0;
    k_c_idx_0.c_w_id = customerWarehouseID;
    k_c_idx_0.c_d_id = customerDistrictID;
    k_c_idx_0.c_last.assign((const char *)lastname_buf, 16);
    k_c_idx_0.c_first.assign(zeros);

    customer_name_idx::key k_c_idx_1;
    k_c_idx_1.c_w_id = customerWarehouseID;
    k_c_idx_1.c_d_id = customerDistrictID;
    k_c_idx_1.c_last.assign((const char *)lastname_buf, 16);
    k_c_idx_1.c_first.assign(ones);

    static_limit_callback<NMaxCustomerIdxScanElems> c(
        s_arena.get(), true);  // probably a safe bet for now

    if (ermia::config::scan_with_it) {
      auto iter =
          ermia::ConcurrentMasstree::ScanIterator</*IsRerverse=*/false>::factory(
              &tbl_customer_name_idx(customerWarehouseID)->GetMasstree(),
              txn->GetXIDContext(), Encode(str(Size(k_c_idx_0)), k_c_idx_0),
              &Encode(str(Size(k_c_idx_1)), k_c_idx_1));
      ermia::dbtuple* tuple = nullptr;
      bool more = iter.init_or_next</*IsNext=*/false>();
      while (more) {
        tuple = ermia::oidmgr->oid_get_version(
            iter.tuple_array(), iter.value(), txn->GetXIDContext());
        if (tuple) {
            rc = txn->DoTupleRead(tuple, &valptr);
            if (rc._val == RC_TRUE) {
                c.Invoke(iter.key().data(), iter.key().length(), valptr);
            }
        }
        more = iter.init_or_next</*IsNext=*/true>();
      }
    } else {
      TryCatch(tbl_customer_name_idx(customerWarehouseID)
                    ->Scan(txn, Encode(str(Size(k_c_idx_0)), k_c_idx_0),
                           &Encode(str(Size(k_c_idx_1)), k_c_idx_1), c));
    }

    ALWAYS_ASSERT(c.size() > 0);
    ASSERT(c.size() < NMaxCustomerIdxScanElems);  // we should detect this
    int index = c.size() / 2;
    if (c.size() % 2 == 0) index--;

    Decode(*c.values[index].second, v_c);
    k_c.c_w_id = customerWarehouseID;
    k_c.c_d_id = customerDistrictID;
    k_c.c_id = v_c.c_id;
  } else {
    // cust by ID
    const uint customerID = GetCustomerId(r);
    k_c.c_w_id = customerWarehouseID;
    k_c.c_d_id = customerDistrictID;
    k_c.c_id = customerID;
    rc = rc_t{RC_INVALID};
    tbl_customer(customerWarehouseID)->GetRecord(txn, rc, Encode(str(Size(k_c)), k_c), valptr);
    TryVerifyRelaxed(rc);
    Decode(valptr, v_c);
  }
#ifndef NDEBUG
  checker::SanityCheckCustomer(&k_c, &v_c);
#endif
  customer::value v_c_new(v_c);

  v_c_new.c_balance -= paymentAmount;
  v_c_new.c_ytd_payment += paymentAmount;
  v_c_new.c_payment_cnt++;
  if (strncmp(v_c.c_credit.data(), "BC", 2) == 0) {
    char buf[501];
    int n = snprintf(buf, sizeof(buf), "%d %d %d %d %d %f | %s", k_c.c_id,
                     k_c.c_d_id, k_c.c_w_id, districtID, warehouse_id,
                     paymentAmount, v_c.c_data.c_str());
    v_c_new.c_data.resize_junk(
        std::min(static_cast<size_t>(n), v_c_new.c_data.max_size()));
    memcpy((void *)v_c_new.c_data.data(), &buf[0], v_c_new.c_data.size());
  }

  TryCatch(tbl_customer(customerWarehouseID)
                ->UpdateRecord(txn, Encode(str(Size(k_c)), k_c),
                      Encode(str(Size(v_c_new)), v_c_new)));

  const history::key k_h(k_c.c_d_id, k_c.c_w_id, k_c.c_id, districtID,
                         warehouse_id, ts);
  history::value v_h;
  v_h.h_amount = paymentAmount;
  v_h.h_data.resize_junk(v_h.h_data.max_size());
  int n = snprintf((char *)v_h.h_data.data(), v_h.h_data.max_size() + 1,
                   "%.10s    %.10s", v_w->w_name.c_str(), v_d->d_name.c_str());
  v_h.h_data.resize_junk(
      std::min(static_cast<size_t>(n), v_h.h_data.max_size()));

  TryCatch(tbl_history(warehouse_id)
                ->InsertRecord(txn, Encode(str(Size(k_h)), k_h),
                         Encode(str(Size(v_h)), v_h)));

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_delivery() {
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint o_carrier_id = RandomNumber(r, 1, NumDistrictsPerWarehouse());
  const uint32_t ts = GetCurrentTimeMillis();

  // worst case txn profile:
  //   10 times:
  //     1 new_order scan node
  //     1 oorder get
  //     2 order_line scan nodes
  //     15 order_line puts
  //     1 new_order remove
  //     1 oorder put
  //     1 customer get
  //     1 customer put
  //
  // output from counters:
  //   max_absent_range_set_size : 0
  //   max_absent_set_size : 0
  //   max_node_scan_size : 21
  //   max_read_set_size : 133
  //   max_write_set_size : 133
  //   num_txn_contexts : 4
  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  for (uint d = 1; d <= NumDistrictsPerWarehouse(); d++) {
    const new_order::key k_no_0(warehouse_id, d, last_no_o_ids[d - 1]);
    const new_order::key k_no_1(warehouse_id, d,
                                std::numeric_limits<int32_t>::max());
    new_order_scan_callback new_order_c;
    {
      TryCatch(tbl_new_order(warehouse_id)
                    ->Scan(txn, Encode(str(Size(k_no_0)), k_no_0),
                           &Encode(str(Size(k_no_1)), k_no_1), new_order_c));
    }

    const new_order::key *k_no = new_order_c.get_key();
    if (unlikely(!k_no)) continue;
    last_no_o_ids[d - 1] = k_no->no_o_id + 1;  // XXX: update last seen

    const oorder::key k_oo(warehouse_id, d, k_no->no_o_id);
    // even if we read the new order entry, there's no guarantee
    // we will read the oorder entry: in this case the txn will abort,
    // but we're simply bailing out early
    oorder::value v_oo_temp;
    ermia::varstr valptr;

    rc_t rc = rc_t{RC_INVALID};
    tbl_oorder(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_oo)), k_oo), valptr);
    TryCatchCondAbort(rc);

    const oorder::value *v_oo = Decode(valptr, v_oo_temp);
#ifndef NDEBUG
    checker::SanityCheckOOrder(&k_oo, v_oo);
#endif

    static_limit_callback<15> c(
        s_arena.get(), false);  // never more than 15 order_lines per order
    const order_line::key k_oo_0(warehouse_id, d, k_no->no_o_id, 0);
    const order_line::key k_oo_1(warehouse_id, d, k_no->no_o_id,
                                 std::numeric_limits<int32_t>::max());

    // XXX(stephentu): mutable scans would help here
    TryCatch(tbl_order_line(warehouse_id)
                  ->Scan(txn, Encode(str(Size(k_oo_0)), k_oo_0),
                         &Encode(str(Size(k_oo_1)), k_oo_1), c));
    float sum = 0.0;
    for (size_t i = 0; i < c.size(); i++) {
      order_line::value v_ol_temp;
      const order_line::value *v_ol = Decode(*c.values[i].second, v_ol_temp);

#ifndef NDEBUG
      order_line::key k_ol_temp;
      const order_line::key *k_ol = Decode(*c.values[i].first, k_ol_temp);
      checker::SanityCheckOrderLine(k_ol, v_ol);
#endif

      sum += v_ol->ol_amount;
      order_line::value v_ol_new(*v_ol);
      v_ol_new.ol_delivery_d = ts;
      ASSERT(s_arena.get()->manages(c.values[i].first));
      TryCatch(tbl_order_line(warehouse_id)
                    ->UpdateRecord(txn, *c.values[i].first,
                          Encode(str(Size(v_ol_new)), v_ol_new)));
    }

    // delete new order
    TryCatch(tbl_new_order(warehouse_id)
                  ->RemoveRecord(txn, Encode(str(Size(*k_no)), *k_no)));

    // update oorder
    oorder::value v_oo_new(*v_oo);
    v_oo_new.o_carrier_id = o_carrier_id;
    TryCatch(tbl_oorder(warehouse_id)
                  ->UpdateRecord(txn, Encode(str(Size(k_oo)), k_oo),
                        Encode(str(Size(v_oo_new)), v_oo_new)));

    const uint c_id = v_oo->o_c_id;
    const float ol_total = sum;

    // update customer
    const customer::key k_c(warehouse_id, d, c_id);
    customer::value v_c_temp;

    rc = rc_t{RC_INVALID};
    tbl_customer(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_c)), k_c), valptr);
    TryVerifyRelaxed(rc);

    const customer::value *v_c = Decode(valptr, v_c_temp);
    customer::value v_c_new(*v_c);
    v_c_new.c_balance += ol_total;
    TryCatch(tbl_customer(warehouse_id)
                  ->UpdateRecord(txn, Encode(str(Size(k_c)), k_c),
                        Encode(str(Size(v_c_new)), v_c_new)));
  }
  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_order_status() {
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse());

  // output from txn counters:
  //   max_absent_range_set_size : 0
  //   max_absent_set_size : 0
  //   max_node_scan_size : 13
  //   max_read_set_size : 81
  //   max_write_set_size : 0
  //   num_txn_contexts : 4
  ermia::transaction *txn = db->NewTransaction(ermia::transaction::TXN_FLAG_READ_ONLY, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  // NB: since txn_order_status() is a RO txn, we assume that
  // locking is un-necessary (since we can just read from some old snapshot)

  customer::key k_c;
  customer::value v_c;
  ermia::varstr valptr;
  if (RandomNumber(r, 1, 100) <= 60) {
    // cust by name
    uint8_t lastname_buf[CustomerLastNameMaxSize + 1];
    static_assert(sizeof(lastname_buf) == 16, "xx");
    memset(lastname_buf, 0, sizeof(lastname_buf));
    GetNonUniformCustomerLastNameRun(lastname_buf, r);

    static const std::string zeros(16, 0);
    static const std::string ones(16, (char)255);

    customer_name_idx::key k_c_idx_0;
    k_c_idx_0.c_w_id = warehouse_id;
    k_c_idx_0.c_d_id = districtID;
    k_c_idx_0.c_last.assign((const char *)lastname_buf, 16);
    k_c_idx_0.c_first.assign(zeros);

    customer_name_idx::key k_c_idx_1;
    k_c_idx_1.c_w_id = warehouse_id;
    k_c_idx_1.c_d_id = districtID;
    k_c_idx_1.c_last.assign((const char *)lastname_buf, 16);
    k_c_idx_1.c_first.assign(ones);

    static_limit_callback<NMaxCustomerIdxScanElems> c(
        s_arena.get(), true);  // probably a safe bet for now
    TryCatch(tbl_customer_name_idx(warehouse_id)
                  ->Scan(txn, Encode(str(Size(k_c_idx_0)), k_c_idx_0),
                         &Encode(str(Size(k_c_idx_1)), k_c_idx_1), c));
    ALWAYS_ASSERT(c.size() > 0);
    ASSERT(c.size() < NMaxCustomerIdxScanElems);  // we should detect this
    int index = c.size() / 2;
    if (c.size() % 2 == 0) index--;

    Decode(*c.values[index].second, v_c);
    k_c.c_w_id = warehouse_id;
    k_c.c_d_id = districtID;
    k_c.c_id = v_c.c_id;
  } else {
    // cust by ID
    const uint customerID = GetCustomerId(r);
    k_c.c_w_id = warehouse_id;
    k_c.c_d_id = districtID;
    k_c.c_id = customerID;

    rc_t rc = rc_t{RC_INVALID};
    tbl_customer(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_c)), k_c), valptr);
    TryVerifyRelaxed(rc);

    Decode(valptr, v_c);
  }
#ifndef NDEBUG
  checker::SanityCheckCustomer(&k_c, &v_c);
#endif

  oorder_c_id_idx::value sv;
  ermia::varstr *newest_o_c_id = s_arena.get()->next(Size(sv));
  if (FLAGS_tpcc_order_status_scan_hack) {
    // XXX(stephentu): HACK- we bound the # of elems returned by this scan to
    // 15- this is because we don't have reverse scans. In an ideal system, a
    // reverse scan would only need to read 1 btree node. We could simulate a
    // lookup by only reading the first element- but then we would *always*
    // read the first order by any customer.  To make this more interesting, we
    // randomly select which elem to pick within the 1st or 2nd btree nodes.
    // This is obviously a deviation from TPC-C, but it shouldn't make that
    // much of a difference in terms of performance numbers (in fact we are
    // making it worse for us)
    latest_key_callback c_oorder(*newest_o_c_id, (r.next() % 15) + 1);
    const oorder_c_id_idx::key k_oo_idx_0(warehouse_id, districtID, k_c.c_id,
                                          0);
    const oorder_c_id_idx::key k_oo_idx_1(warehouse_id, districtID, k_c.c_id,
                                          std::numeric_limits<int32_t>::max());
    {
      TryCatch(tbl_oorder_c_id_idx(warehouse_id)
                    ->Scan(txn, Encode(str(Size(k_oo_idx_0)), k_oo_idx_0),
                           &Encode(str(Size(k_oo_idx_1)), k_oo_idx_1), c_oorder));
    }
    ALWAYS_ASSERT(c_oorder.size());
  } else {
    latest_key_callback c_oorder(*newest_o_c_id, 1);
    const oorder_c_id_idx::key k_oo_idx_hi(warehouse_id, districtID, k_c.c_id,
                                           std::numeric_limits<int32_t>::max());
    TryCatch(tbl_oorder_c_id_idx(warehouse_id)
                  ->ReverseScan(txn, Encode(str(Size(k_oo_idx_hi)), k_oo_idx_hi),
                                nullptr, c_oorder));
    ALWAYS_ASSERT(c_oorder.size() == 1);
  }

  oorder_c_id_idx::key k_oo_idx_temp;
  const oorder_c_id_idx::key *k_oo_idx = Decode(*newest_o_c_id, k_oo_idx_temp);
  const uint o_id = k_oo_idx->o_o_id;

  order_line_nop_callback c_order_line;
  const order_line::key k_ol_0(warehouse_id, districtID, o_id, 0);
  const order_line::key k_ol_1(warehouse_id, districtID, o_id,
                               std::numeric_limits<int32_t>::max());
  TryCatch(tbl_order_line(warehouse_id)
                ->Scan(txn, Encode(str(Size(k_ol_0)), k_ol_0),
                       &Encode(str(Size(k_ol_1)), k_ol_1), c_order_line));
  ALWAYS_ASSERT(c_order_line.n >= 5 && c_order_line.n <= 15);

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_stock_level() {
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint threshold = RandomNumber(r, 10, 20);
  const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse());

  // output from txn counters:
  //   max_absent_range_set_size : 0
  //   max_absent_set_size : 0
  //   max_node_scan_size : 19
  //   max_read_set_size : 241
  //   max_write_set_size : 0
  //   n_node_scan_large_instances : 1
  //   n_read_set_large_instances : 2
  //   num_txn_contexts : 3
  ermia::transaction *txn = db->NewTransaction(ermia::transaction::TXN_FLAG_READ_ONLY, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  // NB: since txn_stock_level() is a RO txn, we assume that
  // locking is un-necessary (since we can just read from some old snapshot)
  const district::key k_d(warehouse_id, districtID);
  district::value v_d_temp;
  ermia::varstr valptr;

  rc_t rc = rc_t{RC_INVALID};
  tbl_district(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_d)), k_d), valptr);
  TryVerifyRelaxed(rc);

  const district::value *v_d = Decode(valptr, v_d_temp);
#ifndef NDEBUG
  checker::SanityCheckDistrict(&k_d, v_d);
#endif

  const uint64_t cur_next_o_id =
      FLAGS_tpcc_new_order_fast_id_gen
          ? NewOrderIdHolder(warehouse_id, districtID)
                .load(std::memory_order_acquire)
          : v_d->d_next_o_id;

  // manual joins are fun!
  order_line_scan_callback c;
  const int32_t lower = cur_next_o_id >= 20 ? (cur_next_o_id - 20) : 0;
  const order_line::key k_ol_0(warehouse_id, districtID, lower, 0);
  const order_line::key k_ol_1(warehouse_id, districtID, cur_next_o_id, 0);
  {
    TryCatch(tbl_order_line(warehouse_id)
                  ->Scan(txn, Encode(str(Size(k_ol_0)), k_ol_0),
                         &Encode(str(Size(k_ol_1)), k_ol_1), c));
  }
  {
    std::unordered_map<uint, bool> s_i_ids_distinct;
    for (auto &p : c.s_i_ids) {
      const stock::key k_s(warehouse_id, p.first);
      stock::value v_s;
      ASSERT(p.first >= 1 && p.first <= NumItems());

      rc = rc_t{RC_INVALID};
      tbl_stock(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_s)), k_s), valptr);
      TryVerifyRelaxed(rc);

      const uint8_t *ptr = (const uint8_t *)valptr.data();
      int16_t i16tmp;
      ptr = serializer<int16_t, true>::read(ptr, &i16tmp);
      if (i16tmp < int(threshold)) s_i_ids_distinct[p.first] = 1;
    }
    // NB(stephentu): s_i_ids_distinct.size() is the computed result of this txn
  }
  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_credit_check() {
  /*
          Note: Cahill's credit check transaction to introduce SI's anomaly.

          SELECT c_balance, c_credit_lim
          INTO :c_balance, :c_credit_lim
          FROM Customer
          WHERE c_id = :c_id AND c_d_id = :d_id AND c_w_id = :w_id

          SELECT SUM(ol_amount) INTO :neworder_balance
          FROM OrderLine, Orders, NewOrder
          WHERE ol_o_id = o_id AND ol_d_id = :d_id
          AND ol_w_id = :w_id AND o_d_id = :d_id
          AND o_w_id = :w_id AND o_c_id = :c_id
          AND no_o_id = o_id AND no_d_id = :d_id
          AND no_w_id = :w_id

          if (c_balance + neworder_balance > c_credit_lim)
          c_credit = "BC";
          else
          c_credit = "GC";

          SQL UPDATE Customer SET c_credit = :c_credit
          WHERE c_id = :c_id AND c_d_id = :d_id AND c_w_id = :w_id
  */
  const uint warehouse_id = pick_wh(r, home_warehouse_id);
  const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse());
  uint customerDistrictID, customerWarehouseID;
  if (likely(FLAGS_tpcc_disable_xpartition_txn || NumWarehouses() == 1 ||
             RandomNumber(r, 1, 100) <= 85)) {
    customerDistrictID = districtID;
    customerWarehouseID = warehouse_id;
  } else {
    customerDistrictID = RandomNumber(r, 1, NumDistrictsPerWarehouse());
    do {
      customerWarehouseID = RandomNumber(r, 1, NumWarehouses());
    } while (customerWarehouseID == warehouse_id);
  }
  ASSERT(!FLAGS_tpcc_disable_xpartition_txn || customerWarehouseID == warehouse_id);

  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);

  // select * from customer with random C_ID
  customer::key k_c;
  customer::value v_c_temp;
  ermia::varstr valptr;
  const uint customerID = GetCustomerId(r);
  k_c.c_w_id = customerWarehouseID;
  k_c.c_d_id = customerDistrictID;
  k_c.c_id = customerID;

  rc_t rc = rc_t{RC_INVALID};
  tbl_customer(customerWarehouseID)->GetRecord(txn, rc, Encode(str(Size(k_c)), k_c), valptr);
  TryVerifyRelaxed(rc);

  const customer::value *v_c = Decode(valptr, v_c_temp);
#ifndef NDEBUG
  checker::SanityCheckCustomer(&k_c, v_c);
#endif

  // scan order
  //		c_w_id = :w_id;
  //		c_d_id = :d_id;
  //		c_id = :c_id;
  credit_check_order_scan_callback c_no(s_arena.get());
  const new_order::key k_no_0(warehouse_id, districtID, 0);
  const new_order::key k_no_1(warehouse_id, districtID,
                              std::numeric_limits<int32_t>::max());
  TryCatch(tbl_new_order(warehouse_id)
                ->Scan(txn, Encode(str(Size(k_no_0)), k_no_0),
                       &Encode(str(Size(k_no_1)), k_no_1), c_no));
  ALWAYS_ASSERT(c_no.output.size());

  double sum = 0;
  for (auto &k : c_no.output) {
    new_order::key k_no_temp;
    const new_order::key *k_no = Decode(*k, k_no_temp);

    const oorder::key k_oo(warehouse_id, districtID, k_no->no_o_id);
    oorder::value v;
    rc = rc_t{RC_INVALID};
    tbl_oorder(warehouse_id)->GetRecord(txn, rc, Encode(str(Size(k_oo)), k_oo), valptr);
    TryCatchCond(rc, continue);
    auto *vv = Decode(valptr, v);

    // Order line scan
    //		ol_d_id = :d_id
    //		ol_w_id = :w_id
    //		ol_o_id = o_id
    //		ol_number = 1-15
    credit_check_order_line_scan_callback c_ol;
    const order_line::key k_ol_0(warehouse_id, districtID, k_no->no_o_id, 1);
    const order_line::key k_ol_1(warehouse_id, districtID, k_no->no_o_id, 15);
    TryCatch(tbl_order_line(warehouse_id)
                  ->Scan(txn, Encode(str(Size(k_ol_0)), k_ol_0),
                         &Encode(str(Size(k_ol_1)), k_ol_1), c_ol));

    /* XXX(tzwang): moved to the callback to avoid storing keys
    ALWAYS_ASSERT(c_ol._v_ol.size());
    for (auto &v_ol : c_ol._v_ol) {
      order_line::value v_ol_temp;
      const order_line::value *val = Decode(*v_ol, v_ol_temp);

      // Aggregation
      sum += val->ol_amount;
    }
    */
    sum += c_ol.sum;
  }

  // c_credit update
  customer::value v_c_new(*v_c);
  if (v_c_new.c_balance + sum >= 5000)  // Threshold = 5K
    v_c_new.c_credit.assign("BC");
  else
    v_c_new.c_credit.assign("GC");
  TryCatch(tbl_customer(customerWarehouseID)
                ->UpdateRecord(txn, Encode(str(Size(k_c)), k_c),
                      Encode(str(Size(v_c_new)), v_c_new)));

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_query2() {
  // TODO(yongjunh): use TXN_FLAG_READ_MOSTLY once SSN's and SSI's read optimization are available.
  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  uint64_t record_count = 0;

  static thread_local tpcc_table_scanner r_scanner(arena);
  r_scanner.clear();
  const region::key k_r_0(0);
  const region::key k_r_1(5);
  TryCatch(tbl_region(1)->Scan(txn, Encode(str(sizeof(k_r_0)), k_r_0),
                                &Encode(str(sizeof(k_r_1)), k_r_1), r_scanner));
  ALWAYS_ASSERT(r_scanner.output.size() == 5);

  static thread_local tpcc_table_scanner n_scanner(arena);
  n_scanner.clear();
  const nation::key k_n_0(0);
  const nation::key k_n_1(std::numeric_limits<int32_t>::max());
  TryCatch(tbl_nation(1)->Scan(txn, Encode(str(sizeof(k_n_0)), k_n_0),
                                &Encode(str(sizeof(k_n_1)), k_n_1), n_scanner));
  ALWAYS_ASSERT(n_scanner.output.size() == 62);

  // Pick a target region
  auto target_region = RandomNumber(r, 0, 4);
  //	auto target_region = 3;
  ALWAYS_ASSERT(0 <= target_region and target_region <= 4);

  // Scan region
  for (auto &r_r : r_scanner.output) {
    region::value v_r_temp;
    const region::value *v_r = Decode(*r_r.second, v_r_temp);

    // filtering region
    if (v_r->r_name != std::string(regions[target_region])) continue;

    region::key k_r_temp;
    const region::key *k_r = Decode(*r_r.first, k_r_temp);
    // Scan nation
    for (auto &r_n : n_scanner.output) {
      nation::value v_n_temp;
      const nation::value *v_n = Decode(*r_n.second, v_n_temp);

      // filtering nation
      if (k_r->r_regionkey != v_n->n_regionkey) continue;

      nation::key k_n_temp;
      const nation::key *k_n = Decode(*r_n.first, k_n_temp);

      // Scan suppliers
      for (auto i = 0; i < FLAGS_tpcc_nr_suppliers; i++) {
        const supplier::key k_su(i);
        supplier::value v_su_tmp;
        ermia::varstr valptr;

        rc_t rc = rc_t{RC_INVALID};
        tbl_supplier(1)->GetRecord(txn, rc, Encode(str(Size(k_su)), k_su), valptr);
        TryVerifyRelaxed(rc);

#if YIELD_ALL
        if (ermia::config::scheduling_policy == 3) {
          ++record_count;
          if (record_count > ermia::config::switch_threshold) {
            record_count = 0;
            swap_context(ermia::thread::Thread::MainContext(), ermia::thread::Thread::PreemptiveContext());
          }
        }
#endif

        arena->return_space(Size(k_su));

        const supplier::value *v_su = Decode(valptr, v_su_tmp);

        // Filtering suppliers
        if (k_n->n_nationkey != v_su->su_nationkey) continue;

        // aggregate - finding a stock tuple having min. stock level
        stock::key min_k_s(0, 0);
        stock::value min_v_s(0, 0, 0, 0);

        int16_t min_qty = std::numeric_limits<int16_t>::max();
        for (auto &it : supp_stock_map[k_su.su_suppkey]) {
          // already know "mod((s_w_id*s_i_id),10000)=su_suppkey" items
          const stock::key k_s(it.first, it.second);
          stock::value v_s_tmp(0, 0, 0, 0);
          rc = rc_t{RC_INVALID};
          tbl_stock(it.first)->GetRecord(txn, rc, Encode(str(Size(k_s)), k_s), valptr);
          TryVerifyRelaxed(rc);

          arena->return_space(Size(k_s));
          const stock::value *v_s = Decode(valptr, v_s_tmp);

          if (ermia::config::scheduling_policy == 3) {
            ++record_count;
            if (record_count > ermia::config::switch_threshold) {
              record_count = 0;
              swap_context(ermia::thread::Thread::MainContext(), ermia::thread::Thread::PreemptiveContext());
            }
          }

          ASSERT(k_s.s_w_id * k_s.s_i_id % 10000 == k_su.su_suppkey);
          if (min_qty > v_s->s_quantity) {
            min_k_s.s_w_id = k_s.s_w_id;
            min_k_s.s_i_id = k_s.s_i_id;
            min_v_s.s_quantity = v_s->s_quantity;
            min_v_s.s_ytd = v_s->s_ytd;
            min_v_s.s_order_cnt = v_s->s_order_cnt;
            min_v_s.s_remote_cnt = v_s->s_remote_cnt;
          }
        }

        // fetch the (lowest stock level) item info
        const item::key k_i(min_k_s.s_i_id);
        item::value v_i_temp;
        rc = rc_t{RC_INVALID};
        tbl_item(1)->GetRecord(txn, rc, Encode(str(Size(k_i)), k_i), valptr);
        TryVerifyRelaxed(rc);

#if YIELD_ALL
        if (ermia::config::scheduling_policy == 3) {
          ++record_count;
          if (record_count > ermia::config::switch_threshold) {
            record_count = 0;
            swap_context(ermia::thread::Thread::MainContext(), ermia::thread::Thread::PreemptiveContext());
          }
        }
#endif

        arena->return_space(Size(k_i));
        const item::value *v_i = Decode(valptr, v_i_temp);
#ifndef NDEBUG
        checker::SanityCheckItem(&k_i, v_i);
#endif
        //  filtering item (i_data like '%b')
        auto found = v_i->i_data.str().find('b');
        if (found != std::string::npos) continue;

        // TODO. sorting by n_name, su_name, i_id

        /*
        cout << k_su.su_suppkey        << ","
                << v_su->su_name                << ","
                << v_n->n_name                  << ","
                << k_i.i_id                     << ","
                << v_i->i_name                  << std::endl;
                */
      }
    }
  }

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

rc_t tpcc_sequential_worker::txn_microbench_random() {
  ermia::transaction *txn = db->NewTransaction(0, *arena, txn_buf());
  ermia::scoped_str_arena s_arena(arena);
  uint start_w = 0, start_s = 0;
  ASSERT(NumWarehouses() * NumItems() >= g_microbench_rows);

  // pick start row, if it's not enough, later wrap to the first row
  uint w = start_w = RandomNumber(r, 1, NumWarehouses());
  uint s = start_s = RandomNumber(r, 1, NumItems());

  // read rows
  ermia::varstr sv;
  for (uint i = 0; i < g_microbench_rows; i++) {
    const stock::key k_s(w, s);
    DLOG(INFO) << "rd " << w << " " << s;
    rc_t rc = rc_t{RC_INVALID};
    tbl_stock(w)->GetRecord(txn, rc, Encode(str(Size(k_s)), k_s), sv);
    TryCatch(rc);

    if (++s > NumItems()) {
      s = 1;
      if (++w > NumWarehouses()) w = 1;
    }
  }

  // now write, in the same read-set
  uint n_write_rows = g_microbench_wr_rows;
  for (uint i = 0; i < n_write_rows; i++) {
    // generate key
    uint row_nr = RandomNumber(
        r, 1, n_write_rows + 1);  // XXX. do we need overlap checking?

    // index starting with 1 is a pain with %, starting with 0 instead:
    // convert row number to (w, s) tuple
    const uint idx =
        (start_w - 1) * NumItems() + (start_s - 1 + row_nr) % NumItems();
    const uint ww = idx / NumItems() + 1;
    const uint ss = idx % NumItems() + 1;

    DLOG(INFO) << (ww - 1) * NumItems() + ss - 1;
    DLOG(INFO) << ((start_w - 1) * NumItems() + start_s - 1 + row_nr) %
                       (NumItems() * (NumWarehouses()));
    ASSERT((ww - 1) * NumItems() + ss - 1 < NumItems() * NumWarehouses());
    ASSERT((ww - 1) * NumItems() + ss - 1 ==
           ((start_w - 1) * NumItems() + (start_s - 1 + row_nr) % NumItems()) %
               (NumItems() * (NumWarehouses())));

    // TODO. more plausible update needed
    const stock::key k_s(ww, ss);
    DLOG(INFO) << "wr " << ww << " " << ss << " row_nr=" << row_nr;

    stock::value v;
    v.s_quantity = RandomNumber(r, 10, 100);
    v.s_ytd = 0;
    v.s_order_cnt = 0;
    v.s_remote_cnt = 0;

#ifndef NDEBUG
    checker::SanityCheckStock(&k_s);
#endif
    TryCatch(tbl_stock(ww)->UpdateRecord(txn, Encode(str(Size(k_s)), k_s),
                                 Encode(str(Size(v)), v)));
  }

  DLOG(INFO) << "micro-random finished";
#ifndef NDEBUG
  abort();
#endif

  TryCatch(db->Commit(txn));
  return {RC_TRUE};
}

bench_worker::workload_desc_vec tpcc_sequential_worker::get_workload() const {
  workload_desc_vec w;
  // numbers from sigmod.csail.mit.edu:
  // w.push_back(workload_desc("NewOrder", 1.0, TxnNewOrder)); // ~10k ops/sec
  // w.push_back(workload_desc("Payment", 1.0, TxnPayment)); // ~32k ops/sec
  // w.push_back(workload_desc("Delivery", 1.0, TxnDelivery)); // ~104k ops/sec
  // w.push_back(workload_desc("OrderStatus", 1.0, TxnOrderStatus)); // ~33k ops/sec
  // w.push_back(workload_desc("StockLevel", 1.0, TxnStockLevel)); // ~2k ops/sec
  unsigned m = 0;

  for (size_t i = 0; i < ARRAY_NELEMS(g_txn_workload_mix); i++) {
    m += g_txn_workload_mix[i];
  }

  ALWAYS_ASSERT(m == 100);

  if (g_txn_workload_mix[0]) {
    w.push_back(workload_desc(
        "NewOrder", double(g_txn_workload_mix[0]) / 100.0, TxnNewOrder));
  }

  if (g_txn_workload_mix[1]) {
    w.push_back(workload_desc(
        "Payment", double(g_txn_workload_mix[1]) / 100.0, TxnPayment));
  }

  if (g_txn_workload_mix[2]) {
    w.push_back(workload_desc("CreditCheck",
                              double(g_txn_workload_mix[2]) / 100.0,
                              TxnCreditCheck));
  }

  if (g_txn_workload_mix[3]) {
    w.push_back(workload_desc(
        "Delivery", double(g_txn_workload_mix[3]) / 100.0, TxnDelivery));
  }

  if (g_txn_workload_mix[4]) {
    w.push_back(workload_desc("OrderStatus",
                              double(g_txn_workload_mix[4]) / 100.0,
                              TxnOrderStatus));
  }

  if (g_txn_workload_mix[5]) {
    w.push_back(workload_desc(
        "StockLevel", double(g_txn_workload_mix[5]) / 100.0, TxnStockLevel));
  }

  if (g_txn_workload_mix[6]) {
    w.push_back(workload_desc("Query2", double(g_txn_workload_mix[6]) / 100.0,
                              TxnQuery2));
  }

  if (g_txn_workload_mix[7]) {
    w.push_back(workload_desc("MicroBenchRandom",
                              double(g_txn_workload_mix[7]) / 100.0,
                              TxnMicroBenchRandom));
  }

  if (ermia::config::scheduling_policy) {
    regular_workload_size = w.size();

    m = 0;
    for (size_t i = 0; i < ARRAY_NELEMS(g_preemptive_txn_workload_mix); i++) {
      m += g_preemptive_txn_workload_mix[i];
    }
    ALWAYS_ASSERT(m == 0 || m == 100);

    if (g_preemptive_txn_workload_mix[0]) {
      w.push_back(workload_desc(
          "Prioritized NewOrder", double(g_preemptive_txn_workload_mix[0]) / 100.0, TxnNewOrder, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[1]) {
      w.push_back(workload_desc(
          "Prioritized Payment", double(g_preemptive_txn_workload_mix[1]) / 100.0, TxnPayment, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[2]) {
      w.push_back(workload_desc("Prioritized CreditCheck",
                                double(g_preemptive_txn_workload_mix[2]) / 100.0,
                                TxnCreditCheck, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[3]) {
      w.push_back(workload_desc(
          "Prioritized Delivery", double(g_preemptive_txn_workload_mix[3]) / 100.0, TxnDelivery, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[4]) {
      w.push_back(workload_desc("Prioritized OrderStatus",
                                double(g_preemptive_txn_workload_mix[4]) / 100.0,
                                TxnOrderStatus, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[5]) {
      w.push_back(workload_desc(
          "Prioritized StockLevel", double(g_preemptive_txn_workload_mix[5]) / 100.0, TxnStockLevel, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[6]) {
      w.push_back(workload_desc("Prioritized Query2", double(g_preemptive_txn_workload_mix[6]) / 100.0,
                                TxnQuery2, nullptr, nullptr, true));
    }

    if (g_preemptive_txn_workload_mix[7]) {
      w.push_back(workload_desc("Prioritized MicroBenchRandom",
                                double(g_preemptive_txn_workload_mix[7]) / 100.0,
                                TxnMicroBenchRandom, nullptr, nullptr, true));
    }
  }

  return w;
}

void tpcc_sequential_worker::MyWork(char *) {
  workload = get_workload();

  // the one extra thread is used as the scheduler thread
  if (worker_id == ermia::config::worker_threads && ermia::config::scheduling_policy) {
    if (ermia::config::scheduling_policy == 2) { // 1: vanilla, 2: preemptive, 3: yield
retry:
      std::lock_guard<std::mutex> guard(ermia::receiver_fd_map_lock);
      if (ermia::receiver_fd_map.size() < ermia::config::worker_threads) {
        goto retry;
      }

      for (int i = 0; i < ermia::config::worker_threads; ++i) {
        int sender_idx = uintr_register_sender(ermia::receiver_fd_map[i], 0);
        if (sender_idx < 0) {
          printf("[ERROR] Failed to register sender.\n");
          exit(1);
        }
        printf("[INFO] Sender created channel[%d]\n", sender_idx);
        ermia::sender_idx_map[i] = sender_idx;
      }
    }
    for (int i = 0; i < ermia::config::worker_threads; ++i) {
      auto worker = bench_runner::workers[i];
      worker->main_workload_queue.init(ermia::config::main_queue_size);
      worker->preemptive_workload_queue.init(ermia::config::prioritized_queue_size);
    }

    global_workload_queue.init(ermia::config::global_queue_size);
    barrier_a->count_down();
    barrier_b->wait_for();

    auto next_time = std::chrono::high_resolution_clock::now() + std::chrono::microseconds(ermia::config::arrival_interval_us);
    while (running) {
      // TODO: policy
      for (int i = 0; i < ermia::config::worker_threads; ++i) {
        auto worker = bench_runner::workers[i];
        if (!worker->main_workload_queue.isFull()) {
          auto workload_index = fetch_workload();
          worker->main_workload_queue.push({workload_index, util::timer()});
        }
      }
      if (has_prioritized_workload() && std::chrono::high_resolution_clock::now() >= next_time) {
        // 0. update next_time
        next_time = std::chrono::high_resolution_clock::now() + std::chrono::microseconds(ermia::config::arrival_interval_us);

        // 1. push preemptive transactions to the global queue
        while (!global_workload_queue.isFull()) {
          auto workload_index = fetch_preemptive_workload();
          auto pair = std::make_pair(workload_index, util::timer());
          pair.second.start_global();
          global_workload_queue.push(pair);
        }

        // 2. move preemptive transactions from the global queue to the worker's preemptive queue
        int worker_id = 0;
        while (!global_workload_queue.isEmpty() && running) {
          auto worker = bench_runner::workers[worker_id];
          if(worker->main_workload_queue.isEmpty()){
            auto workload_index = fetch_workload();
            worker->main_workload_queue.push({workload_index, util::timer()});
          }
          if (ermia::config::scheduling_policy > 1 && ermia::thread::Thread::MainContext(worker_id)->starved()){
            worker_id = (worker_id + 1) % ermia::config::worker_threads;
            continue;
          }

          while (!worker->preemptive_workload_queue.isFull() && !global_workload_queue.isEmpty()) {
            auto txn = global_workload_queue.front();
            txn.second.start_local();
            worker->preemptive_workload_queue.push(txn);
            global_workload_queue.pop();
          }

          if (ermia::config::scheduling_policy == 2) {
            _senduipi(ermia::sender_idx_map[worker_id]);
          }

          worker_id = (worker_id + 1) % ermia::config::worker_threads;
        }
      }
    }

    if (ermia::config::scheduling_policy == 2) {
      for (int i = 0; i < ermia::config::worker_threads; ++i) {
        uintr_unregister_sender(ermia::sender_idx_map[i], 0);
      }
    }

    return;
  }
  if (is_worker) {
    tlog = ermia::GetLog();
    txn_counts.resize(workload.size());

    // register uintr handler
    if (ermia::config::scheduling_policy == 2 || ermia::config::scheduling_policy == 3) {
      preemptive_worker->workload = preemptive_worker->get_workload();
      preemptive_worker->txn_counts.resize(preemptive_worker->workload.size());
      //FIXME: tlog????

      pcontext::Set_Worker_Id(worker_id);

      ermia::thread::Thread::PreemptiveContext()->SetRSP(init_stack(ermia::thread::Thread::PreemptiveStack(), reinterpret_cast<void *>(&bench_worker::static_preemptive_transaction)));
      ermia::thread::Thread::PreemptiveContext()->new_context = true;
      ermia::thread::Thread::PreemptiveContext()->stack_start = (uint64_t)ermia::thread::Thread::PreemptiveStackStart();
      ermia::thread::Thread::PreemptiveContext()->stack_end = (uint64_t)ermia::thread::Thread::PreemptiveStackEnd();
      ermia::thread::Thread::PreemptiveContext()->fs = me->shadow_thread->fs_base_register;
      ermia::thread::Thread::PreemptiveContext()->gs = me->shadow_thread->gs_base_register;
      pthread_attr_t attr;
      void* stackAddr;
      size_t stackSize;
      // Get the thread's attributes
      pthread_getattr_np(pthread_self(), &attr);
      // Get the stack address and size
      pthread_attr_getstack(&attr, &stackAddr, &stackSize);
      // Calculate the stack end address
      void* stackEnd = static_cast<char*>(stackAddr) + stackSize;
      ermia::thread::Thread::MainContext()->stack_start = (uint64_t)stackAddr;
      ermia::thread::Thread::MainContext()->stack_end = (uint64_t)stackEnd;
      ermia::thread::Thread::MainContext()->fs = _readfsbase_u64();
      ermia::thread::Thread::MainContext()->gs = _readgsbase_u64();

      std::lock_guard<std::mutex> guard(ermia::receiver_fd_map_lock);
      uintr_register_handler(interrupt_handler_func, 0);
      printf("[INFO] Worker[%d] registered handler\n", worker_id);

      int receiver_fd = uintr_create_fd(worker_id, 0);
      if (receiver_fd < 0) {
        perror(NULL);
        printf("[ERROR] Worker[%d] failed to create uintr fd.\n", worker_id);
        exit(1);
      }
      printf("[INFO] Worker[%d] created receiver fd %d\n", worker_id, receiver_fd);
      ermia::receiver_fd_map[worker_id] = receiver_fd;
    }

    barrier_a->count_down();
    barrier_b->wait_for();

    if (ermia::config::scheduling_policy > 1) {
      ermia::thread::Thread::MainContext()->reset_timer();
    }
    
    while (running) {
      if (ermia::config::scheduling_policy) {
        // do preemptive transaction first, if any
        _clui();
        uint32_t stat = 0;
        stat |= !preemptive_workload_queue.isEmpty() ? 1 : 0;
        stat |= !main_workload_queue.isEmpty() ? (1<<1) : 0;
        if(ermia::config::scheduling_policy > 1){
          stat |= ermia::thread::Thread::MainContext()->starved() ? (1<<2) : 0;
        }
        // stat = [starved, main_queue_not_empty, preemptive_queue_not_empty]
        if(stat == 0b000 || stat == 0b100){
          continue;
        }
        if(stat == 0b001 || stat == 0b011 || stat == 0b101){
          std::pair<uint32_t, util::timer> preemptive_head = preemptive_workload_queue.front();
          do_workload_function(preemptive_head.first, preemptive_head.second, true);
          preemptive_workload_queue.pop();
        } else {  // stat == 0b111, 0b110, 0b101
          std::pair<uint32_t, util::timer> main_head = main_workload_queue.front();
          _stui();
          if (ermia::config::scheduling_policy > 1) {
            ermia::thread::Thread::MainContext()->reset_timer();
          }
          do_workload_function(main_head.first, main_head.second);
          if (ermia::config::scheduling_policy > 1) {
            ermia::thread::Thread::MainContext()->reset_timer();
          }
          main_workload_queue.pop();
        }
      } else {
        main_workload_queue.push(std::make_pair(fetch_workload(), util::timer()));
        std::pair<uint32_t, util::timer> main_head = main_workload_queue.front();
        do_workload_function(main_head.first, main_head.second);
        main_workload_queue.pop();
      }
    }
  }
}

void tpcc_do_test(ermia::Engine *db) {
  ermia::config::read_txn_type = "tpcc-sequential";
  tpcc_parse_options();
  tpcc_bench_runner<tpcc_sequential_worker> r(db);
  r.run();
}

int main(int argc, char **argv) {
  bench_main(argc, argv, tpcc_do_test);
  return 0;
}

#endif // NOT NESTED_COROUTINE && NOT HYBRID_COROUTINE
