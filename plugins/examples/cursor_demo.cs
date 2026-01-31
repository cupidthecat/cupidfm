// cursor_tracker.cs - Example plugin demonstrating fm.on_editor_cursor_move API
//
// This plugin tracks cursor movements in the editor and displays statistics.
// It demonstrates how to use the on_editor_cursor_move callback to monitor
// cursor position changes in real-time.

let move_count = 0;
let total_line_moves = 0;
let total_col_moves = 0;
let last_line = 0;
let last_col = 0;

// Called when the plugin loads
fn on_load() {
    fm.console("Cursor Tracker plugin loaded");
    fm.console("Press Ctrl+M in the editor to see cursor movement statistics");
    
    // Bind Ctrl+M to show statistics
    fm.bind("^K", "show_cursor_stats");
}

// Called when the cursor moves in the editor
fn on_editor_cursor_move(old_line, old_col, new_line, new_col) {
    move_count = move_count + 1;
    
    // Calculate movement distances
    let line_delta = new_line - old_line;
    let col_delta = new_col - old_col;
    
    // Track absolute movement
    if (line_delta < 0) {
        line_delta = -line_delta;
    }
    if (col_delta < 0) {
        col_delta = -col_delta;
    }
    
    total_line_moves = total_line_moves + line_delta;
    total_col_moves = total_col_moves + col_delta;
    
    last_line = new_line;
    last_col = new_col;
    
    // Show notification every 50 moves (optional - can be commented out if too noisy)
    // if (move_count % 50 == 0) {
    //     fm.notify(fmt("Cursor moves: %d", move_count));
    // }
}

// Show cursor movement statistics
fn show_cursor_stats(key) {
    if (!fm.editor_active()) {
        fm.notify("Not in editor mode");
        return true;
    }
    
    let avg_line_move = 0;
    let avg_col_move = 0;
    
    if (move_count > 0) {
        avg_line_move = total_line_moves / move_count;
        avg_col_move = total_col_moves / move_count;
    }
    
    // Build statistics message
    let stats = list();
    push(stats, fmt("=== Cursor Movement Statistics ==="));
    push(stats, fmt("Total cursor moves: %d", move_count));
    push(stats, fmt("Current position: Line %d, Col %d", last_line, last_col));
    push(stats, fmt("Total line movements: %d", total_line_moves));
    push(stats, fmt("Total column movements: %d", total_col_moves));
    push(stats, fmt("Avg line move distance: %v", avg_line_move));
    push(stats, fmt("Avg column move distance: %v", avg_col_move));
    
    // Display in console
    for msg in stats {
        fm.console(msg);
    }
    
    // Also show a notification
    fm.notify(fmt("Cursor moves: %d (see console for details)", move_count));
    
    return true;
}

// Called when a file is opened in the editor
fn on_editor_open(path) {
    // Reset statistics for new file
    move_count = 0;
    total_line_moves = 0;
    total_col_moves = 0;
    last_line = 1;
    last_col = 1;
    
    fm.console(fmt("Cursor tracking started for: %s", path));
}
