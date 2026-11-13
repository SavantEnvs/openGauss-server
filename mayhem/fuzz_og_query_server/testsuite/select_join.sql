select a.id, count(*) from t1 a join t2 b on a.id = b.id where a.x > 10 and b.y like '%z%' group by a.id having count(*) > 1 order by 1 limit 5;
