(* Boxes per minute: counts the rising edges of `arrived` and publishes
   the count once a minute. Written in ST and used as a box in the KOP
   program: `arrived` (its first BOOL input) takes the rung's power, and
   `tooFast` (its first BOOL output) gives it back. *)
FUNCTION_BLOCK BoxRate
VAR_INPUT
    arrived : BOOL;
    limit : INT := 30;
END_VAR
VAR_OUTPUT
    tooFast : BOOL;
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
    tooFast := perMinute > limit;
END_FUNCTION_BLOCK
