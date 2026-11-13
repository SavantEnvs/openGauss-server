merge into t1 using t2 on (t1.id = t2.id) when matched then update set t1.v = t2.v when not matched then insert values (t2.id, t2.v);
