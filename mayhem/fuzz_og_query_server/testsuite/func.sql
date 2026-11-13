select coalesce(a, 0), cast(b as numeric(10,2)), case when c then 1 else 2 end, to_char(now(), 'YYYY-MM-DD') from t1;
