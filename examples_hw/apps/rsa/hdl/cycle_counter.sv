module cycle_counter (
    input  logic        clk,          // Clock
    input  logic        resetn,       // Active-low reset
    input  logic        start,        // Start counting (pulse)
    input  logic        stop,         // Stop counting (pulse)
    output logic [31:0] cycle_count,  // Elapsed cycles
    output logic        count_valid   // Valid pulse when stopped
);

typedef enum {IDLE, COUNTING} state_t;
state_t state;

// Counter and synchronization registers
logic [31:0] counter;
logic start_sync, stop_sync;
logic prev_start, prev_stop;

// Edge detection for start/stop signals
always_ff @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        {prev_start, prev_stop} <= 2'b00;
        {start_sync, stop_sync} <= 2'b00;
    end else begin
        // Synchronizer chain for input signals
        prev_start <= start;
        prev_stop <= stop;
        start_sync <= prev_start;
        stop_sync <= prev_stop;
    end
end

// Rising edge detection
wire start_pulse = start_sync && !prev_start;
wire stop_pulse = stop_sync && !prev_stop;

// Main state machine and counter
always_ff @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        state <= IDLE;
        counter <= 0;
        cycle_count <= 0;
        count_valid <= 0;
    end else begin
        count_valid <= 0;  // Default to invalid
        
        case (state)
            IDLE: begin
                if (start_pulse) begin
                    state <= COUNTING;
                    counter <= 0;
                end
            end
            
            COUNTING: begin
                counter <= counter + 1;
                
                if (stop_pulse) begin
                    state <= IDLE;
                    cycle_count <= counter;
                    count_valid <= 1;
                end
            end
        endcase
    end
end

endmodule