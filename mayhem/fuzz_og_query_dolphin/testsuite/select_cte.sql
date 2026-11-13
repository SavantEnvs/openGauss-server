with recursive r(n) as (select 1 union all select n+1 from r where n < 10) select n from r;
