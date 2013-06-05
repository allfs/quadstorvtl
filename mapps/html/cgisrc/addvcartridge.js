<!-- 

function isNumeric(str){
	var re = /[\D]/
	if (re.test(str))
	{
		return false;
	}
	return true;
}

function ValidString(str){
	var re = /[^a-zA-Z0-9]/

	if (re.test(str))
	{
		return false;
	}
	return true;
}

function checkform()
{
	var frm = document.getElementById('addvcartridge');

	if (!frm.barcode.value) {
		alert("Media label cannot be empty");
		return false;
	}

	if (!ValidString(frm.barcode.value))
	{
		alert("Media label can only contains alphabets or numbers");
		return false;
	}

	if (!frm.vtlname.value)
	{
		alert("VTL Name cannot be empty");
		return false;
	}

	if (!ValidString(frm.vtlname.value))
	{
		alert("VTL Name can only contains alphabets or numbers");
	}

	if (!frm.nvolumes.value)
	{
		alert("Number of volumes cannot be empty");
		return false;
	}

	if (!isNumeric(frm.nvolumes.value))
	{
		alert("Number of volumes should be a number");
		return false;
	}

	var nvolumes = parseInt(frm.nvolumes.value);

	if (nvolumes <= 0)
	{
		alert("Number of volumes has to be a number greater than zero");
		return false;
	}

	if (nvolumes > 512)
	{
		alert("Number of media has to be a number less than 512");
		return false;
	}

	if (frm.barcode.value.length != 6 && nvolumes != 1)
	{
		alert("Barcode prefix has to be 6 characters");
		return false;
	}
	return true;
}

// -->
