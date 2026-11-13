insert into t1(a,b) values (1,'x'),(2,'y') on duplicate key update b = values(b);
