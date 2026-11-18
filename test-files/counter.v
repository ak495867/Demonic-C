module Counter8 (
    input clk,
    input rst,
    input en,
    output reg [7:0] out
);

reg [7:0] count;

always @(posedge clk or posedge rst) begin
    if (rst) begin
        count <= 0;
    end else if (en) begin
        count <= count + 1;
    end
end

always @(*) begin
    out = count;
end

endmodule
