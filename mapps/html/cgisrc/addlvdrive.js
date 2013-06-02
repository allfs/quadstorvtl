<!-- 
var vtldrivetypes = new Array(
	new Array("ADIC Scalar 24", 0x07, 0x10, 0x11),
	new Array("ADIC Scalar 100", 0x07, 0x08, 0x10, 0x11, 0x12),
	new Array("ADIC Scalar i2000", 0x07, 0x08, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15),
	new Array("HP StorageWorks ESL9000", 0x06, 0x07, 0x09, 0x0B, 0xC),
	new Array("HP StorageWorks ESL E-Series", 0x04, 0x05, 0x09, 0x0B, 0x0C, 0x0D),
	new Array("HP StorageWorks EML E-Series", 0x0B, 0x0C, 0x0D),
	new Array("IBM 3583 Ultrium Scalable Library", 0x10, 0x11, 0x12),
	new Array("IBM 3584 Ultra Scalable Library", 0x10, 0x11, 0x12, 0x13, 0x14, 0x15),
	new Array("IBM System Storage TS3100", 0x10, 0x11, 0x12, 0x13, 0x14),
	new Array("HP StorageWorks MSL 2024/4048/8096", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF),
	new Array("HP StorageWorks MSL 6000", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF),
	new Array("Overland NEO 2000/4000/8000 Series", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF)
);

var drivetypes = new Array(
	new Array("HP StorageWorks DLT VS80", 0x1),
	new Array("HP StorageWorks DLT VS160", 0x2),
	new Array("HP StorageWorks SDLT 220", 0x3),
	new Array("HP StorageWorks SDLT 320", 0x4),
	new Array("HP StorageWorks SDLT 600", 0x5),
	new Array("Quantum SDLT 220", 0x6),
	new Array("Quantum SDLT 320", 0x7),
	new Array("Quantum SDLT 600", 0x8),
	new Array("HP StorageWorks Ultrium 232", 0x9),
	new Array("HP StorageWorks Ultrium 448", 0xA),
	new Array("HP StorageWorks Ultrium 460", 0xB),
	new Array("HP StorageWorks Ultrium 960", 0xC),
	new Array("HP StorageWorks Ultrium 1840", 0xD),
	new Array("HP StorageWorks Ultrium 3280", 0xE),
	new Array("HP StorageWorks Ultrium 6250", 0xF),
	new Array("IBM 3580 Ultrium1", 0x10),
	new Array("IBM 3580 Ultrium2", 0x11),
	new Array("IBM 3580 Ultrium3", 0x12),
	new Array("IBM 3580 Ultrium4", 0x13),
	new Array("IBM 3580 Ultrium5", 0x14),
	new Array("IBM 3580 Ultrium6", 0x15)
);

function fillvtldrivetypes(myform)
{
	var frm = document.getElementById('addlvdrive');
	var i;
	var darray;

	if (frm.driveselect.options)
		frm.driveselect.options.length = 0;
	index = frm.vselect.value - 1;

	darray = vtldrivetypes[index];
	for (i = 1; i < darray.length; i++)
	{
		var dtype = darray[i] - 1;
		var vtlval = drivetypes[dtype];

		frm.driveselect.options[i-1] = new Option(vtlval[0], vtlval[1], true, false);
	}
}

function fillinit()
{
	fillvtldrivetypes();
}

function isNumeric(str){
	var re = /[\D]/
	if (re.test(str))
	{
		return false;
	}
	return true;
}

function checkform()
{
	var frm = document.getElementById('addlvdrive');
	var foundtype;

	ndrives = parseInt(frm.ndrives.value);
	drivetype = frm.driveselect.value;
	drivetype = parseInt(drivetype);

	prevndrivetypes = parseInt(frm.ndrivetypes.value);
	var count = 0;
	for (i = 0; i < prevndrivetypes; i++)
	{
		ndtypesstr = "ndrivetype"+i;
		prevval = frm.elements[ndtypesstr].value;
		count += parseInt(prevval);
	}

	foundtype = 0;
	for (i = 0; i < prevndrivetypes; i++)
	{
		dtypestr = "drivetype"+i;

		dtypeval = frm.elements[dtypestr].value;
		if (parseInt(dtypeval) == drivetype)
		{
			foundtype = 1;
			ndtypesstr = "ndrivetype"+i;
			prevval = frm.elements[ndtypesstr].value;

			prevval = parseInt(prevval)+ndrives+'';
			frm.elements[ndtypesstr].value = prevval;
			break;
		}

	}

	if (!foundtype)
	{
		var newstr = "drivetype"+prevndrivetypes;
		elem = frm.elements[newstr];

		elem.value = frm.driveselect.value; 
		newstr = "ndrivetype"+prevndrivetypes;
		elem = frm.elements[newstr];
		elem.value = frm.ndrives.value; 

		frm.ndrivetypes.value = prevndrivetypes+1+'';
	}

	return true;
}

// -->
