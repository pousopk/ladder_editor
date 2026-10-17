(* Boxes per minute: counts the rising edges of `arrived` and publishes
   the count once a minute. Written in ST next to the KOP program; calling
   it from a rung comes once boxes can use the project's own blocks. *)
FUNCTION_BLOCK BoxRate
VAR_INPUT
    arrived : BOOL;
END_VAR
VAR_OUTPUT
    perMinute : INT;
END_VAR
VAR
    edge : R_TRIG;
    window : TON;
    count : INT;
END_VAR
    edge(CLK := arrived);
    IF edge.Q THEN
        count := count + 1;
    END_IF;
    window(IN := NOT window.Q, PT := T#1m);
    IF window.Q THEN
        perMinute := count;
        count := 0;
    END_IF;
END_FUNCTION_BLOCK
