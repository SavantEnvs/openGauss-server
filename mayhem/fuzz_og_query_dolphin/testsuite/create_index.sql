create unique index idx_t1_a on t1 using btree (a desc, lower(b)) where a > 0;
