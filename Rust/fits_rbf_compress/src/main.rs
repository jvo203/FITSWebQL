use fitsio::FitsFile;
use std::env;

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
    } else {
        println!("bitpix: {}", bitpix);
    }

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
        Ok(naxis1) => match naxis1.parse::<i32>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS1 not found, aborting");
            return;
        }
    };

    let height = match hdu.read_key::<String>(&mut fits, "NAXIS2") {
        Ok(naxis2) => match naxis2.parse::<i32>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS2 not found, aborting");
            return;
        }
    };

    let depth = match hdu.read_key::<String>(&mut fits, "NAXIS3") {
        Ok(naxis3) => match naxis3.parse::<i32>() {
            Ok(x) => x,
            Err(_) => 0,
        },
        Err(_) => {
            println!("NAXIS3 not found, defaulting to 1");
            1
        }
    };

    println!("width: {}, height: {}, depth: {}", width, height, depth);
}
