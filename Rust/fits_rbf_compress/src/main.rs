#![allow(non_snake_case)]

use fitsio::FitsFile;
use std::cmp::{max, min};
use std::env;
use time as precise_time;

use ocl::Buffer;
use ocl::ProQue;
use rand::distributions::{StandardNormal, Uniform};
use rand::prelude::*;
use rand::seq::SliceRandom;

const TILE_SIZE: usize = 256;

fn main() {
    let args: Vec<String> = env::args().collect();

    let fits = if args.len() > 1 {
        &args[1]
    } else {
        println!("Radial Basis Function neural network compression of FITS files. Usage: fits_rbf_compress <FITS file>");
        return;
    };

    let mut fits = FitsFile::open(fits).unwrap();
    fits.pretty_print().unwrap();

    let hdu = fits.hdu(0).unwrap();
    println!("{:?}", hdu);

    let bitpix = match hdu.read_key::<String>(&mut fits, "BITPIX") {
        Ok(bitpix) => match bitpix.parse::<i32>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS not found, aborting");
            return;
        }
    };

    if bitpix != -32 {
        println!("unsupported bitpix({}), aborting", bitpix);
        return;
    };

    let naxis = match hdu.read_key::<String>(&mut fits, "NAXIS") {
        Ok(naxis) => match naxis.parse::<i32>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS not found, aborting");
            return;
        }
    };

    if naxis < 2 {
        println!("expected at least a 2D image, aborting");
        return;
    }

    let width = match hdu.read_key::<String>(&mut fits, "NAXIS1") {
        Ok(naxis1) => match naxis1.parse::<usize>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS1 not found, aborting");
            return;
        }
    };

    let height = match hdu.read_key::<String>(&mut fits, "NAXIS2") {
        Ok(naxis2) => match naxis2.parse::<usize>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS2 not found, aborting");
            return;
        }
    };

    let depth = match hdu.read_key::<String>(&mut fits, "NAXIS3") {
        Ok(naxis3) => match naxis3.parse::<usize>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS3 not found, defaulting to 1");
            1
        }
    };

    println!(
        "width: {}, height: {}, depth: {}, bitpix: {}",
        width, height, depth, bitpix
    );

    if width <= 0 || height <= 0 {
        println!("width and height need to be positive, aborting");
        return;
    }

    let capacity = width * height;
    let mut success: bool = true;

    //a depth override for test purposes
    for frame in 0..1 {
        //for frame in 0..depth {
        println!(
            "compressing frame {}/{}, #float32 pixels: {}",
            frame + 1,
            depth,
            capacity
        );

        let offset = frame * capacity;
        match hdu.read_section::<Vec<f32>>(&mut fits, offset, offset + capacity) {
            Ok(data) => {
                let nrows = (height as f32 / TILE_SIZE as f32).ceil() as usize;
                let ncols = (width as f32 / TILE_SIZE as f32).ceil() as usize;

                println!(
                    "data length: {} pixels, TILE SIZE: {}, nrows: {}, ncols: {}",
                    data.len(),
                    TILE_SIZE,
                    nrows,
                    ncols
                );

                for row in 0..nrows {
                    for col in 0..ncols {
                        let x1 = col * TILE_SIZE;
                        let x2 = min(width, x1 + TILE_SIZE);
                        let y1 = row * TILE_SIZE;
                        let y2 = min(height, y1 + TILE_SIZE);

                        let mut tile: Vec<f32> = Vec::new();

                        for x in x1..x2 {
                            for y in y1..y2 {
                                tile.push(data[y * width + x]);
                            }
                        }

                        println!(
                            "processing row {} column {} :> x₁={} x₂={} y₁={} y₂={}, tile length: {}",
                            row, col, x1, x2, y1, y2, tile.len()
                        );

                        success = success && rbf_compress_tile(&tile, x2 - x1, y2 - y1);
                    }
                }
            }
            Err(err) => {
                println!("FITS data reading error: {}", err);
                success = success && false;
            }
        };
    }

    if success {
        println!("compression successful.");
    }
}

fn rbf_compress_tile(tile: &Vec<f32>, width: usize, height: usize) -> bool {
    let capacity = width * height;

    if capacity != tile.len() || tile.len() == 0 {
        println!(
            "incorrect tile dimensions: width: {}, height: {}, size: {}",
            width,
            height,
            tile.len()
        );
        return false;
    };

    //get tile min/max values
    let mut tile_min = std::f32::MAX;
    let mut tile_max = std::f32::MIN;
    let mut is_nan = true;

    for x in tile {
        if x.is_finite() {
            is_nan = is_nan && false;

            if *x > tile_max {
                tile_max = *x;
            }

            if *x < tile_min {
                tile_min = *x;
            }
        };
    }

    println!(
        "is_nan: {}, tile_min: {}, tile_max: {}",
        is_nan, tile_min, tile_max
    );

    if is_nan {
        return true;
    }

    //prepare the training data
    let mut data: Vec<f32> = Vec::with_capacity(capacity);
    let mut x1: Vec<f32> = Vec::with_capacity(capacity);
    let mut x2: Vec<f32> = Vec::with_capacity(capacity);
    let mut y: Vec<f32> = Vec::with_capacity(capacity);
    let mut e: Vec<f32> = Vec::with_capacity(capacity);
    let mut count: usize = 0;

    for iy in 0..height {
        for ix in 0..width {
            let tmp = tile[count];
            count = count + 1;

            if tmp.is_finite() {
                let pixel = 0.5_f32 + (tmp - tile_min) / (tile_max - tile_min);
                data.push(pixel.ln());

                let val1 = (ix as f32) / ((width - 1) as f32);
                let val2 = (iy as f32) / ((height - 1) as f32);

                x1.push(val1);
                x2.push(val2);
                y.push(0.0);
                e.push(0.0);
            }
        }
    }

    //init RBF clusters
    let XCLUST = min(width / 8, 32);
    let YCLUST = min(height / 8, 32);
    let NCLUST = max(1, min(count / 64, 1024));

    println!("valid pixel count: {}, NCLUST: {}", count, NCLUST);

    let mut p0: Vec<f32> = Vec::with_capacity(NCLUST);
    let mut p1: Vec<f32> = Vec::with_capacity(NCLUST);
    let mut p2: Vec<f32> = Vec::with_capacity(NCLUST);

    let mut rng = thread_rng();
    let angle = Uniform::new(0.0f32, 2.0 * std::f32::consts::PI);

    let c1: Vec<f32> = x1.choose_multiple(&mut rng, NCLUST).cloned().collect();
    let c2: Vec<f32> = x2.choose_multiple(&mut rng, NCLUST).cloned().collect();

    for _ in 0..NCLUST {
        let sigmaX = 0.1f32 / ((XCLUST - 1) as f32);
        let sigmaY = 0.1f32 / ((YCLUST - 1) as f32);
        let theta = angle.sample(&mut rng);

        let a = 0.5 * theta.cos() * theta.cos() / (sigmaX * sigmaX)
            + 0.5 * theta.sin() * theta.sin() / (sigmaY * sigmaY);
        let b = -0.25 * (2.0 * theta).sin() / (sigmaX * sigmaX)
            + 0.25 * (2.0 * theta).sin() / (sigmaY * sigmaY);
        let c = 0.5 * theta.sin() * theta.sin() / (sigmaX * sigmaX)
            + 0.5 * theta.cos() * theta.cos() / (sigmaY * sigmaY);

        p0.push(a.ln());
        p1.push(b);
        p2.push(c.ln());
    }

    let normal = StandardNormal;
    //no. clusters plus a bias term
    let mut w: Vec<f32> = (0..NCLUST + 1)
        .map(|_| normal.sample(&mut rng) as f32)
        .collect();

    let etap = 1.2_f32;
    let etam = 0.5_f32;
    let dMin = 1e-5_f32;
    let dMax = 1e-1_f32;
    let d0 = dMin;

    //previous gradients
    let mut grad_c1_prev: Vec<f32> = vec![0.0; NCLUST];
    let mut grad_c2_prev: Vec<f32> = vec![0.0; NCLUST];
    let mut grad_p0_prev: Vec<f32> = vec![0.0; NCLUST];
    let mut grad_p1_prev: Vec<f32> = vec![0.0; NCLUST];
    let mut grad_p2_prev: Vec<f32> = vec![0.0; NCLUST];
    let mut grad_w_prev: Vec<f32> = vec![0.0; NCLUST + 1];

    //changes
    let mut dc1: Vec<f32> = vec![d0; NCLUST];
    let mut dc2: Vec<f32> = vec![d0; NCLUST];
    let mut dp0: Vec<f32> = vec![d0; NCLUST];
    let mut dp1: Vec<f32> = vec![d0; NCLUST];
    let mut dp2: Vec<f32> = vec![d0; NCLUST];
    let mut dw: Vec<f32> = vec![d0; NCLUST + 1];

    //define NCLUST dynamically
    let mut ocl_src = format!("#define NCLUST {}", NCLUST);

    ocl_src.push_str(r#"
        inline void atomicAdd_l_f(volatile __local float *addr, float val)
        {
            union {
                unsigned int u32;
                float f32;
            } next, expected, current;
            current.f32 = *addr;
            do
            {
                expected.f32 = current.f32;
                next.f32 = expected.f32 + val;
                current.u32 = atomic_cmpxchg((volatile __local unsigned int *)addr,
                                     expected.u32, next.u32);
            } while (current.u32 != expected.u32);
        }

        inline void atomicAdd_g_f(volatile __global float *addr, float val)
        {
            union {
                unsigned int u32;
                float f32;
            } next, expected, current;
            current.f32 = *addr;
            do
            {
                expected.f32 = current.f32;
                next.f32 = expected.f32 + val;
                current.u32 = atomic_cmpxchg((volatile __global unsigned int *)addr,
                                     expected.u32, next.u32);
            } while (current.u32 != expected.u32);
        }

        __kernel void rbf_gradient(__global float *_x1, __global float *_x2, __global float *_y, __global float *_data, __global float *_e, __constant float *c1, __constant float *c2, __constant float *p0, __constant float *p1, __constant float *p2, __constant float *w, __global float *_grad_c1, __global float *_grad_c2, __global float *_grad_p0, __global float *_grad_p1, __global float *_grad_p2, __global float *_grad_w)
        {

            __local float tid_grad_c1[NCLUST];
            __local float tid_grad_c2[NCLUST];
            __local float tid_grad_p0[NCLUST];
            __local float tid_grad_p1[NCLUST];
            __local float tid_grad_p2[NCLUST];
            __local float tid_grad_w[NCLUST + 1];

            float grad_c1[NCLUST];
            float grad_c2[NCLUST];
            float grad_p0[NCLUST];
            float grad_p1[NCLUST];
            float grad_p2[NCLUST];
            float grad_w[NCLUST + 1];

            size_t local_index = get_local_id(0);

            if (local_index == 0)
            {
                for (int i = 0; i < NCLUST; i++)
                {
                    tid_grad_c1[i] = 0.0;
                    tid_grad_c2[i] = 0.0;
                    tid_grad_p0[i] = 0.0;
                    tid_grad_p1[i] = 0.0;
                    tid_grad_p2[i] = 0.0;
                }

                for (int i = 0; i < NCLUST + 1; i++)
                    tid_grad_w[i] = 0.0;
            };
            barrier(CLK_LOCAL_MEM_FENCE);

            size_t index = get_global_id(0);

            float x1 = _x1[index];
            float x2 = _x2[index];

            float tmp = w[NCLUST];
            grad_w[NCLUST] = 1.0; //bias

            for (int i = 0; i < NCLUST; i++)
            {
                float a = native_exp(p0[i]);
                float b = p1[i];
                float c = native_exp(p2[i]);

                float tmp1 = (x1 - c1[i]);
                float tmp2 = (x2 - c2[i]);
                float dist = a * tmp1 * tmp1 - 2.0f * b * tmp1 * tmp2 + c * tmp2 * tmp2;
                float act = native_exp(-dist);
                tmp += w[i] * act;

                //gradients
                grad_w[i] = act;
                grad_c1[i] = 2.0 * w[i] * act * (a * tmp1 - b * tmp2);
                grad_c2[i] = 2.0 * w[i] * act * (c * tmp2 - b * tmp1);
                grad_p0[i] = w[i] * act * a * (-tmp1 * tmp1);
                grad_p1[i] = 2.0 * w[i] * act * tmp1 * tmp2;
                grad_p2[i] = w[i] * act * c * (-tmp2 * tmp2);
            }

            float e = tmp - _data[index];
            _y[index] = tmp;
            _e[index] = e;

            //gradients
            for (int i = 0; i < NCLUST + 1; i++)
                atomicAdd_l_f(&(tid_grad_w[i]), e * grad_w[i]);

            for (int i = 0; i < NCLUST; i++)
            {
                atomicAdd_l_f(&(tid_grad_c1[i]), e * grad_c1[i]);
                atomicAdd_l_f(&(tid_grad_c2[i]), e * grad_c2[i]);
                atomicAdd_l_f(&(tid_grad_p0[i]), e * grad_p0[i]);
                atomicAdd_l_f(&(tid_grad_p1[i]), e * grad_p1[i]);
                atomicAdd_l_f(&(tid_grad_p2[i]), e * grad_p2[i]);
            }

            barrier(CLK_LOCAL_MEM_FENCE);
            if (local_index == (get_local_size(0) - 1))
            {
                for (int i = 0; i < NCLUST + 1; i++)
                    atomicAdd_g_f(&(_grad_w[i]), tid_grad_w[i]);

                for (int i = 0; i < NCLUST; i++)
                {
                    atomicAdd_g_f(&(_grad_c1[i]), tid_grad_c1[i]);
                    atomicAdd_g_f(&(_grad_c2[i]), tid_grad_c2[i]);
                    atomicAdd_g_f(&(_grad_p0[i]), tid_grad_p0[i]);
                    atomicAdd_g_f(&(_grad_p1[i]), tid_grad_p1[i]);
                    atomicAdd_g_f(&(_grad_p2[i]), tid_grad_p2[i]);
                }
            }
        }
    "#);

    //init OpenCL buffers
    let pro_que = match ProQue::builder().src(ocl_src.clone()).dims(count).build() {
        Ok(queue) => queue,
        Err(err) => {
            println!("{}", err);
            return true;
        }
    };

    let ocl_data = pro_que.create_buffer::<f32>().unwrap();
    ocl_data.write(&data).enq().unwrap();

    let ocl_x1 = pro_que.create_buffer::<f32>().unwrap();
    ocl_x1.write(&x1).enq().unwrap();

    let ocl_x2 = pro_que.create_buffer::<f32>().unwrap();
    ocl_x2.write(&x2).enq().unwrap();

    let ocl_y = pro_que.create_buffer::<f32>().unwrap();
    let ocl_e = pro_que.create_buffer::<f32>().unwrap();

    let kernel = pro_que
        .kernel_builder("rbf_gradient")
        .arg(&ocl_x1)
        .arg(&ocl_x2)
        .arg(&ocl_y)
        .arg(&ocl_data)
        .arg(&ocl_e)
        .arg_named("c1", None::<&Buffer<f32>>)
        .arg_named("c2", None::<&Buffer<f32>>)
        .arg_named("p0", None::<&Buffer<f32>>)
        .arg_named("p1", None::<&Buffer<f32>>)
        .arg_named("p2", None::<&Buffer<f32>>)
        .arg_named("w", None::<&Buffer<f32>>)
        .arg_named("_grad_c1", None::<&Buffer<f32>>)
        .arg_named("_grad_c2", None::<&Buffer<f32>>)
        .arg_named("_grad_p0", None::<&Buffer<f32>>)
        .arg_named("_grad_p1", None::<&Buffer<f32>>)
        .arg_named("_grad_p2", None::<&Buffer<f32>>)
        .arg_named("_grad_w", None::<&Buffer<f32>>)
        /*.arg(&ocl_c1)
        .arg(&ocl_c2)
        .arg(&ocl_p0)
        .arg(&ocl_p1)
        .arg(&ocl_p2)
        .arg(&ocl_w)
        .arg(&ocl_grad_c1)
        .arg(&ocl_grad_c2)
        .arg(&ocl_grad_p0)
        .arg(&ocl_grad_p1)
        .arg(&ocl_grad_p2)
        .arg(&ocl_grad_w)*/
        .build()
        .unwrap();

    //batch training
    let NITER = 500;

    for iter in 0..NITER {
        print!("{}/{} ", iter + 1, NITER);

        let start = precise_time::precise_time_ns();

        //reset the gradients to zero
        let mut grad_c1: Vec<f32> = vec![0.0; NCLUST];
        let mut grad_c2: Vec<f32> = vec![0.0; NCLUST];
        let mut grad_p0: Vec<f32> = vec![0.0; NCLUST];
        let mut grad_p1: Vec<f32> = vec![0.0; NCLUST];
        let mut grad_p2: Vec<f32> = vec![0.0; NCLUST];
        let mut grad_w: Vec<f32> = vec![0.0; NCLUST + 1];

        let ocl_grad_c1 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_grad_c1.write(&grad_c1).enq().unwrap();

        let ocl_grad_c2 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_grad_c2.write(&grad_c2).enq().unwrap();

        let ocl_grad_p0 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_grad_p0.write(&grad_p0).enq().unwrap();

        let ocl_grad_p1 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_grad_p1.write(&grad_p1).enq().unwrap();

        let ocl_grad_p2 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_grad_p2.write(&grad_p2).enq().unwrap();

        let ocl_grad_w = pro_que
            .buffer_builder::<f32>()
            .len(NCLUST + 1)
            .build()
            .unwrap();

        //reset the param buffers
        let ocl_c1 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_c1.write(&c1).enq().unwrap();

        let ocl_c2 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_c2.write(&c2).enq().unwrap();

        let ocl_p0 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_p0.write(&p0).enq().unwrap();

        let ocl_p1 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_p1.write(&p1).enq().unwrap();

        let ocl_p2 = pro_que.buffer_builder::<f32>().len(NCLUST).build().unwrap();
        ocl_p2.write(&p2).enq().unwrap();

        let ocl_w = pro_que
            .buffer_builder::<f32>()
            .len(NCLUST + 1)
            .build()
            .unwrap();
        ocl_w.write(&w).enq().unwrap();

        //set named arguments to the kernel
        kernel.set_arg("c1", &ocl_c1).unwrap();
        kernel.set_arg("c2", &ocl_c2).unwrap();
        kernel.set_arg("p0", &ocl_p0).unwrap();
        kernel.set_arg("p1", &ocl_p1).unwrap();
        kernel.set_arg("p2", &ocl_p2).unwrap();
        kernel.set_arg("w", &ocl_w).unwrap();
        kernel.set_arg("_grad_c1", &ocl_grad_c1).unwrap();
        kernel.set_arg("_grad_c2", &ocl_grad_c2).unwrap();
        kernel.set_arg("_grad_p0", &ocl_grad_p0).unwrap();
        kernel.set_arg("_grad_p1", &ocl_grad_p1).unwrap();
        kernel.set_arg("_grad_p2", &ocl_grad_p2).unwrap();
        kernel.set_arg("_grad_w", &ocl_grad_w).unwrap();

        unsafe {
            kernel.enq().unwrap();
        }

        ocl_y.read(&mut y).enq().unwrap();
        ocl_e.read(&mut e).enq().unwrap();
        ocl_grad_c1.read(&mut grad_c1).enq().unwrap();
        ocl_grad_c2.read(&mut grad_c2).enq().unwrap();
        ocl_grad_p0.read(&mut grad_p0).enq().unwrap();
        ocl_grad_p1.read(&mut grad_p1).enq().unwrap();
        ocl_grad_p2.read(&mut grad_p2).enq().unwrap();
        ocl_grad_w.read(&mut grad_w).enq().unwrap();

        let stop = precise_time::precise_time_ns();

        println!(
            "[OpenCL::rbf_gradient] elapsed time: {} [ms]",
            (stop - start) / 1000000
        );

        //update the parameters
    }

    return true;
}
