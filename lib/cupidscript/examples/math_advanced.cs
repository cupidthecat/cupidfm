// Advanced Math Examples
// Real-world applications: physics simulations, statistics, game development, finance

println("=== Statistical Functions ===");

fn mean(numbers) {
    let sum = 0;
    for num in numbers {
        sum += num;
    }
    return sum / len(numbers);
}

fn variance(numbers) {
    let avg = mean(numbers);
    let sum_sq_diff = 0;
    for num in numbers {
        let diff = num - avg;
        sum_sq_diff += diff * diff;
    }
    return sum_sq_diff / len(numbers);
}

fn std_dev(numbers) {
    return sqrt(variance(numbers));
}

fn median(numbers) {
    let sorted = [x for x in numbers];
    sort(sorted);
    let n = len(sorted);
    if (n % 2 == 1) {
        return sorted[floor(n / 2)];
    } else {
        let mid1 = sorted[floor(n / 2) - 1];
        let mid2 = sorted[floor(n / 2)];
        return (mid1 + mid2) / 2.0;
    }
}

let data = [23, 45, 12, 67, 34, 89, 56, 23, 45];
println("Data:", data);
println("Mean:", mean(data));
println("Median:", median(data));
println("Std Dev:", std_dev(data));
println();

println("=== Monte Carlo Simulation: Estimating PI ===");

fn estimate_pi(samples) {
    let inside_circle = 0;
    for i in range(samples) {
        let x = random() * 2 - 1;  // -1 to 1
        let y = random() * 2 - 1;  // -1 to 1
        if (x * x + y * y <= 1) {
            inside_circle += 1;
        }
    }
    return 4.0 * inside_circle / samples;
}

let pi_estimate = estimate_pi(10000);
println("Estimated PI (10000 samples):", pi_estimate);
println("Actual PI:", PI);
println("Error:", abs(PI - pi_estimate));
println();

println("=== 2D Vector Math ===");

fn vec_add(v1, v2) {
    return {x: v1.x + v2.x, y: v1.y + v2.y};
}

fn vec_sub(v1, v2) {
    return {x: v1.x - v2.x, y: v1.y - v2.y};
}

fn vec_scale(v, scalar) {
    return {x: v.x * scalar, y: v.y * scalar};
}

fn vec_length(v) {
    return sqrt(v.x * v.x + v.y * v.y);
}

fn vec_normalize(v) {
    let len = vec_length(v);
    if (len == 0) { return {x: 0, y: 0}; }
    return {x: v.x / len, y: v.y / len};
}

fn vec_dot(v1, v2) {
    return v1.x * v2.x + v1.y * v2.y;
}

fn vec_angle(v1, v2) {
    let dot = vec_dot(v1, v2);
    let len1 = vec_length(v1);
    let len2 = vec_length(v2);
    return acos(dot / (len1 * len2));
}

let a = {x: 3, y: 4};
let b = {x: 1, y: 0};
println("Vector a:", a);
println("Vector b:", b);
println("a + b:", vec_add(a, b));
println("a - b:", vec_sub(a, b));
println("Length of a:", vec_length(a));
println("Normalized a:", vec_normalize(a));
println("Angle between a and b:", vec_angle(a, b), "radians");
println();

println("=== Physics: Projectile Motion ===");

fn projectile_trajectory(v0, angle_deg, dt) {
    let g = 9.8;
    let angle = angle_deg * PI / 180;
    let vx = v0 * cos(angle);
    let vy = v0 * sin(angle);
    
    let trajectory = [];
    let t = 0;
    let y = 0;
    
    while (y >= 0 || t == 0) {
        let x = vx * t;
        y = vy * t - 0.5 * g * t * t;
        if (y >= 0) {
            push(trajectory, {x: x, y: y, t: t});
        }
        t += dt;
    }
    
    return trajectory;
}

let projectile = projectile_trajectory(30, 45, 0.5);
println("Projectile (v0=30 m/s, angle=45°):");
println("Points calculated:", len(projectile));
println("First point:", projectile[0]);
println("Peak:", projectile[floor(len(projectile) / 2)]);
println("Last point:", projectile[len(projectile) - 1]);
println();

println("=== Financial Calculations ===");

// Compound interest
fn compound_interest(principal, rate, time, n) {
    return principal * pow(1 + rate / n, n * time);
}

// Continuous compound interest
fn continuous_compound(principal, rate, time) {
    return principal * exp(rate * time);
}

// Loan payment (annuity)
fn loan_payment(principal, rate, periods) {
    let r = rate / 12;  // monthly rate
    return principal * (r * pow(1 + r, periods)) / (pow(1 + r, periods) - 1);
}

let investment = 1000;
let annual_rate = 0.05;
let years = 10;

println("Investment:", investment);
println("Annual rate:", annual_rate * 100, "%");
println("Years:", years);
println("Compound (quarterly):", compound_interest(investment, annual_rate, years, 4));
println("Compound (continuous):", continuous_compound(investment, annual_rate, years));

let loan_amount = 20000;
let loan_rate = 0.06;
let loan_years = 5;
let monthly_payment = loan_payment(loan_amount, loan_rate, loan_years * 12);
println();
println("Loan:", loan_amount);
println("Rate:", loan_rate * 100, "%");
println("Term:", loan_years, "years");
println("Monthly payment:", round(monthly_payment * 100) / 100);
println();

println("=== Game Development: Circular Motion ===");

fn orbit_position(radius, angle_speed, time) {
    let angle = angle_speed * time;
    return {
        x: radius * cos(angle),
        y: radius * sin(angle),
        angle: angle
    };
}

println("Orbital motion (radius=5, speed=0.5 rad/s):");
for t in range(0, 10, 2) {
    let pos = orbit_position(5, 0.5, t);
    println("  t=" + to_str(t) + ":", 
            "x=" + to_str(round(pos.x * 100) / 100) + 
            ", y=" + to_str(round(pos.y * 100) / 100));
}
println();

println("=== Random Walk Simulation ===");

fn random_walk_2d(steps, step_size) {
    let path = [{x: 0, y: 0}];
    let x = 0;
    let y = 0;
    
    for i in range(steps) {
        let angle = random() * 2 * PI;
        x += step_size * cos(angle);
        y += step_size * sin(angle);
        push(path, {x: x, y: y});
    }
    
    return path;
}

let walk = random_walk_2d(100, 1);
let final_pos = walk[len(walk) - 1];
let final_dist = sqrt(final_pos.x * final_pos.x + final_pos.y * final_pos.y);
println("Random walk (100 steps):");
println("Final position:", final_pos);
println("Distance from origin:", final_dist);
println();

println("=== Dice Rolling Statistics ===");

fn roll_dice(num_dice, num_sides) {
    let total = 0;
    for i in range(num_dice) {
        total += random_int(1, num_sides);
    }
    return total;
}

fn simulate_dice_rolls(num_dice, num_sides, trials) {
    let results = map();
    
    for i in range(trials) {
        let roll = roll_dice(num_dice, num_sides);
        let key = to_str(roll);
        if (mhas(results, key)) {
            mset(results, key, mget(results, key) + 1);
        } else {
            mset(results, key, 1);
        }
    }
    
    return results;
}

println("Rolling 2d6 (two six-sided dice) 1000 times:");
let dice_results = simulate_dice_rolls(2, 6, 1000);
let sorted_keys = keys(dice_results);
sort(sorted_keys, fn(a, b) => to_int(a) - to_int(b));

for key in sorted_keys {
    let count = mget(dice_results, key);
    let percentage = round(count / 10.0 * 10) / 10;
    let bar = str_repeat("█", round(count / 20));
    println("  " + key + ": " + to_str(count) + " (" + to_str(percentage) + "%) " + bar);
}
println();

println("=== Signal Processing: Simple Low-Pass Filter ===");

fn low_pass_filter(signal, alpha) {
    let filtered = [signal[0]];
    for i in range(1, len(signal)) {
        let new_val = alpha * signal[i] + (1 - alpha) * filtered[i - 1];
        push(filtered, new_val);
    }
    return filtered;
}

// Generate noisy signal
let noisy_signal = [];
for i in range(20) {
    let clean = sin(i * 0.3);
    let noise = (random() - 0.5) * 0.5;
    push(noisy_signal, clean + noise);
}

let filtered_signal = low_pass_filter(noisy_signal, 0.3);
println("Signal filtering (first 5 points):");
for i in range(5) {
    println("  Noisy:", round(noisy_signal[i] * 100) / 100, 
            "→ Filtered:", round(filtered_signal[i] * 100) / 100);
}
println();

println("Advanced math examples complete!");
