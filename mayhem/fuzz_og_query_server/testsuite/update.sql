update t1 set a = a + 1, b = 'q' where id in (select id from t2);
